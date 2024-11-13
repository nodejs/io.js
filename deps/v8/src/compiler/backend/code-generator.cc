// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/code-generator.h"

#include <optional>

#include "src/base/bounds.h"
#include "src/base/iterator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/globals.h"
#include "src/compiler/linkage.h"
#include "src/compiler/pipeline.h"
#include "src/deoptimizer/translated-state.h"
#include "src/diagnostics/eh-frame.h"
#include "src/execution/frames.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/code-kind.h"
#include "src/objects/smi.h"
#include "src/utils/address-map.h"
#include "src/utils/utils.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-deopt-data.h"
#endif

namespace v8 {
namespace internal {
namespace compiler {

class CodeGenerator::JumpTable final : public ZoneObject {
 public:
  JumpTable(JumpTable* next, Label** targets, size_t target_count)
      : next_(next), targets_(targets), target_count_(target_count) {}

  Label* label() { return &label_; }
  JumpTable* next() const { return next_; }
  Label** targets() const { return targets_; }
  size_t target_count() const { return target_count_; }

 private:
  Label label_;
  JumpTable* const next_;
  Label** const targets_;
  size_t const target_count_;
};

CodeGenerator::CodeGenerator(Zone* codegen_zone, Frame* frame, Linkage* linkage,
                             InstructionSequence* instructions,
                             OptimizedCompilationInfo* info, Isolate* isolate,
                             std::optional<OsrHelper> osr_helper,
                             int start_source_position,
                             JumpOptimizationInfo* jump_opt,
                             const AssemblerOptions& options, Builtin builtin,
                             size_t max_unoptimized_frame_height,
                             size_t max_pushed_argument_count,
                             const char* debug_name)
    : zone_(codegen_zone),
      isolate_(isolate),
      frame_access_state_(nullptr),
      linkage_(linkage),
      instructions_(instructions),
      unwinding_info_writer_(codegen_zone),
      info_(info),
      labels_(codegen_zone->AllocateArray<Label>(
          instructions->InstructionBlockCount())),
      current_block_(RpoNumber::Invalid()),
      start_source_position_(start_source_position),
      current_source_position_(SourcePosition::Unknown()),
      masm_(isolate, options, CodeObjectRequired::kNo,
            std::unique_ptr<AssemblerBuffer>{}),
      resolver_(this),
      safepoints_(codegen_zone),
      handlers_(codegen_zone),
      deoptimization_exits_(codegen_zone),
      deoptimization_literals_(codegen_zone),
      translations_(codegen_zone),
      max_unoptimized_frame_height_(max_unoptimized_frame_height),
      max_pushed_argument_count_(max_pushed_argument_count),
      caller_registers_saved_(false),
      jump_tables_(nullptr),
      ools_(nullptr),
      osr_helper_(std::move(osr_helper)),
      osr_pc_offset_(-1),
      source_position_table_builder_(
          codegen_zone, SourcePositionTableBuilder::RECORD_SOURCE_POSITIONS),
#if V8_ENABLE_WEBASSEMBLY
      protected_instructions_(codegen_zone),
#endif  // V8_ENABLE_WEBASSEMBLY
      result_(kSuccess),
      block_starts_(codegen_zone),
      instr_starts_(codegen_zone),
      debug_name_(debug_name) {
  for (int i = 0; i < instructions->InstructionBlockCount(); ++i) {
    new (&labels_[i]) Label;
  }
  CreateFrameAccessState(frame);
  CHECK_EQ(info->is_osr(), osr_helper_.has_value());
  masm_.set_jump_optimization_info(jump_opt);
  CodeKind code_kind = info->code_kind();
  if (code_kind == CodeKind::WASM_FUNCTION ||
      code_kind == CodeKind::WASM_TO_CAPI_FUNCTION ||
      code_kind == CodeKind::WASM_TO_JS_FUNCTION ||
      code_kind == CodeKind::JS_TO_WASM_FUNCTION) {
    masm_.set_abort_hard(true);
  }
  masm_.set_builtin(builtin);
}

void CodeGenerator::RecordProtectedInstruction(uint32_t instr_offset) {
#if V8_ENABLE_WEBASSEMBLY
  protected_instructions_.push_back({instr_offset});
#endif  // V8_ENABLE_WEBASSEMBLY
}

void CodeGenerator::CreateFrameAccessState(Frame* frame) {
  FinishFrame(frame);
  frame_access_state_ = zone()->New<FrameAccessState>(frame);
}

bool CodeGenerator::ShouldApplyOffsetToStackCheck(Instruction* instr,
                                                  uint32_t* offset) {
  DCHECK_EQ(instr->arch_opcode(), kArchStackPointerGreaterThan);

  StackCheckKind kind =
      static_cast<StackCheckKind>(MiscField::decode(instr->opcode()));
  if (kind != StackCheckKind::kJSFunctionEntry) return false;

  uint32_t stack_check_offset = *offset = GetStackCheckOffset();
  return stack_check_offset > kStackLimitSlackForDeoptimizationInBytes;
}

uint32_t CodeGenerator::GetStackCheckOffset() {
  if (!frame_access_state()->has_frame()) {
    DCHECK_EQ(max_unoptimized_frame_height_, 0);
    DCHECK_EQ(max_pushed_argument_count_, 0);
    return 0;
  }

  size_t incoming_parameter_count =
      linkage_->GetIncomingDescriptor()->ParameterSlotCount();
  DCHECK(is_int32(incoming_parameter_count));
  int32_t optimized_frame_height =
      static_cast<int32_t>(incoming_parameter_count) * kSystemPointerSize +
      frame()->GetTotalFrameSlotCount() * kSystemPointerSize;
  DCHECK(is_int32(max_unoptimized_frame_height_));
  int32_t signed_max_unoptimized_frame_height =
      static_cast<int32_t>(max_unoptimized_frame_height_);

  // The offset is either the delta between the optimized frames and the
  // interpreted frame, or the maximal number of bytes pushed to the stack
  // while preparing for function calls, whichever is bigger.
  uint32_t frame_height_delta = static_cast<uint32_t>(std::max(
      signed_max_unoptimized_frame_height - optimized_frame_height, 0));
  uint32_t max_pushed_argument_bytes =
      static_cast<uint32_t>(max_pushed_argument_count_ * kSystemPointerSize);
  return std::max(frame_height_delta, max_pushed_argument_bytes);
}

CodeGenerator::CodeGenResult CodeGenerator::AssembleDeoptimizerCall(
    DeoptimizationExit* exit) {
  int deoptimization_id = exit->deoptimization_id();
  if (deoptimization_id > Deoptimizer::kMaxNumberOfEntries) {
    return kTooManyDeoptimizationBailouts;
  }

  DeoptimizeKind deopt_kind = exit->kind();
  DeoptimizeReason deoptimization_reason = exit->reason();
  Label* jump_deoptimization_entry_label =
      &jump_deoptimization_entry_labels_[static_cast<int>(deopt_kind)];
  if (info()->source_positions()) {
    masm()->RecordDeoptReason(deoptimization_reason, exit->node_id(),
                              exit->pos(), deoptimization_id);
  }

  if (deopt_kind == DeoptimizeKind::kLazy) {
    ++lazy_deopt_count_;
    masm()->BindExceptionHandler(exit->label());
  } else {
    ++eager_deopt_count_;
    masm()->bind(exit->label());
  }
  Builtin target = Deoptimizer::GetDeoptimizationEntry(deopt_kind);
  masm()->CallForDeoptimization(target, deoptimization_id, exit->label(),
                                deopt_kind, exit->continue_label(),
                                jump_deoptimization_entry_label);

  exit->set_emitted();

  return kSuccess;
}

void CodeGenerator::MaybeEmitOutOfLineConstantPool() {
  masm()->MaybeEmitOutOfLineConstantPool();
}

void CodeGenerator::AssembleCode() {
  OptimizedCompilationInfo* info = this->info();
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  // Compute incoming parameter count for code using JS linkage. This will
  // ultimately set the parameter count on the resulting Code object.
  if (call_descriptor->IsJSFunctionCall()) {
    parameter_count_ = call_descriptor->ParameterSlotCount();
#ifdef DEBUG
    if (Builtins::IsBuiltinId(info->builtin())) {
      DCHECK_EQ(parameter_count_,
                Builtins::GetStackParameterCount(info->builtin()));
    } else if (info->has_bytecode_array()) {
      DCHECK_EQ(parameter_count_, info->bytecode_array()->parameter_count());
    }
#endif  // DEBUG
  }

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // MANUAL indicates that the scope shouldn't actually generate code to set up
  // the frame (that is done in AssemblePrologue).
  FrameScope frame_scope(masm(), StackFrame::MANUAL);

  if (info->source_positions()) {
    AssembleSourcePosition(start_source_position());
  }
  offsets_info_.code_start_register_check = masm()->pc_offset();

  masm()->CodeEntry();

  // Check that {kJavaScriptCallCodeStartRegister} has been set correctly.
  if (v8_flags.debug_code && info->called_with_code_start_register()) {
    masm()->RecordComment("-- Prologue: check code start register --");
    AssembleCodeStartRegisterCheck();
  }

#if V8_ENABLE_WEBASSEMBLY
  if (info->code_kind() == CodeKind::WASM_TO_JS_FUNCTION ||
      info->builtin() == Builtin::kWasmToJsWrapperCSA) {
    // By default the code generator can convert slot IDs to SP-relative memory
    // operands depending on the offset. However, the SP may switch to the
    // central stack at the beginning of the wrapper if the caller is on a
    // secondary stack, and switch back at the end. So for the whole duration
    // of the wrapper, only FP-relative addressing is valid.
    frame_access_state()->SetFPRelativeOnly(true);
  }
#endif

  offsets_info_.deopt_check = masm()->pc_offset();
  // We want to bailout only from JS functions, which are the only ones
  // that are optimized.
  if (info->IsOptimizing()) {
    DCHECK(call_descriptor->IsJSFunctionCall());
    masm()->RecordComment("-- Prologue: check for deoptimization --");
    BailoutIfDeoptimized();
  }

  // Define deoptimization literals for all inlined functions.
  DCHECK_EQ(0u, deoptimization_literals_.size());
  for (OptimizedCompilationInfo::InlinedFunctionHolder& inlined :
       info->inlined_functions()) {
    if (!inlined.shared_info.equals(info->shared_info())) {
      int index = DefineDeoptimizationLiteral(
          DeoptimizationLiteral(inlined.shared_info));
      inlined.RegisterInlinedFunctionId(index);
    }
  }
  inlined_function_count_ = deoptimization_literals_.size();

  // Define deoptimization literals for all BytecodeArrays to which we might
  // deopt to ensure they are strongly held by the optimized code.
  if (info->has_bytecode_array()) {
    DefineDeoptimizationLiteral(DeoptimizationLiteral(info->bytecode_array()));
  }
  for (OptimizedCompilationInfo::InlinedFunctionHolder& inlined :
       info->inlined_functions()) {
    if (!inlined.bytecode_array.is_null()) {
      DefineDeoptimizationLiteral(
          DeoptimizationLiteral(inlined.bytecode_array));
    } else {
      // Inlined wasm functions do not have a bytecode array.
      DCHECK(info->inline_js_wasm_calls());
    }
  }

  unwinding_info_writer_.SetNumberOfInstructionBlocks(
      instructions()->InstructionBlockCount());

  if (info->trace_turbo_json()) {
    block_starts_.assign(instructions()->instruction_blocks().size(), -1);
    instr_starts_.assign(instructions()->instructions().size(), {});
  }
  // Assemble instructions in assembly order.
  offsets_info_.blocks_start = masm()->pc_offset();
  for (const InstructionBlock* block : instructions()->ao_blocks()) {
    // Align loop headers on vendor recommended boundaries.
    if (block->ShouldAlignLoopHeader()) {
      masm()->LoopHeaderAlign();
    } else if (block->ShouldAlignCodeTarget()) {
      masm()->CodeTargetAlign();
    }

    if (info->trace_turbo_json()) {
      block_starts_[block->rpo_number().ToInt()] = masm()->pc_offset();
    }
    // Bind a label for a block.
    current_block_ = block->rpo_number();
    unwinding_info_writer_.BeginInstructionBlock(masm()->pc_offset(), block);
    if (v8_flags.code_comments && !block->omitted_by_jump_threading()) {
      std::ostringstream buffer;
      buffer << "-- B" << block->rpo_number().ToInt() << " start";
      if (block->IsDeferred()) buffer << " (deferred)";
      if (!block->needs_frame()) buffer << " (no frame)";
      if (block->must_construct_frame()) buffer << " (construct frame)";
      if (block->must_deconstruct_frame()) buffer << " (deconstruct frame)";

      if (block->IsLoopHeader()) {
        buffer << " (loop up to " << block->loop_end().ToInt() << ")";
      }
      if (block->loop_header().IsValid()) {
        buffer << " (in loop " << block->loop_header().ToInt() << ")";
      }
      buffer << " --";
      masm()->RecordComment(buffer.str().c_str(), SourceLocation());
    }

    frame_access_state()->MarkHasFrame(block->needs_frame());

    masm()->bind(GetLabel(current_block_));

    if (block->must_construct_frame()) {
      AssembleConstructFrame();
      // We need to setup the root register after we assemble the prologue, to
      // avoid clobbering callee saved registers in case of C linkage and
      // using the roots.
      // TODO(mtrofin): investigate how we can avoid doing this repeatedly.
      if (call_descriptor->InitializeRootRegister()) {
        masm()->InitializeRootRegister();
      }
    }
#ifdef CAN_USE_RVV_INSTRUCTIONS
    // RVV uses VectorUnit to emit vset{i}vl{i}, reducing the static and dynamic
    // overhead of the vset{i}vl{i} instruction. However there are some jumps
    // back between blocks. the Rvv instruction may get an incorrect vtype. so
    // here VectorUnit needs to be cleared to ensure that the vtype is correct
    // within the block.
    masm()->VU.clear();
#endif
    if (V8_EMBEDDED_CONSTANT_POOL_BOOL && !block->needs_frame()) {
      ConstantPoolUnavailableScope constant_pool_unavailable(masm());
      result_ = AssembleBlock(block);
    } else {
      result_ = AssembleBlock(block);
    }
    if (result_ != kSuccess) return;
    unwinding_info_writer_.EndInstructionBlock(block);
  }

  // Assemble all out-of-line code.
  offsets_info_.out_of_line_code = masm()->pc_offset();
  if (ools_) {
    masm()->RecordComment("-- Out of line code --");
    for (OutOfLineCode* ool = ools_; ool; ool = ool->next()) {
      masm()->bind(ool->entry());
      ool->Generate();
      if (ool->exit()->is_bound()) masm()->jmp(ool->exit());
    }
  }

  // This nop operation is needed to ensure that the trampoline is not
  // confused with the pc of the call before deoptimization.
  // The test regress/regress-259 is an example of where we need it.
  masm()->nop();

  // For some targets, we must make sure that constant and veneer pools are
  // emitted before emitting the deoptimization exits.
  PrepareForDeoptimizationExits(&deoptimization_exits_);

  deopt_exit_start_offset_ = masm()->pc_offset();

  // Assemble deoptimization exits.
  offsets_info_.deoptimization_exits = masm()->pc_offset();
  int last_updated = 0;
  // We sort the deoptimization exits here so that the lazy ones will be visited
  // last. We need this as lazy deopts might need additional instructions.
  auto cmp = [](const DeoptimizationExit* a, const DeoptimizationExit* b) {
    // The deoptimization exits are sorted so that lazy deopt exits appear after
    // eager deopts.
    static_assert(static_cast<int>(DeoptimizeKind::kLazy) ==
                      static_cast<int>(kLastDeoptimizeKind),
                  "lazy deopts are expected to be emitted last");
    if (a->kind() != b->kind()) {
      return a->kind() < b->kind();
    }
    return a->pc_offset() < b->pc_offset();
  };
  std::sort(deoptimization_exits_.begin(), deoptimization_exits_.end(), cmp);

  {
#ifdef V8_TARGET_ARCH_PPC64
    v8::internal::Assembler::BlockTrampolinePoolScope block_trampoline_pool(
        masm());
#endif
    for (DeoptimizationExit* exit : deoptimization_exits_) {
      if (exit->emitted()) continue;
      exit->set_deoptimization_id(next_deoptimization_id_++);
      result_ = AssembleDeoptimizerCall(exit);
      if (result_ != kSuccess) return;

      // UpdateDeoptimizationInfo expects lazy deopts to be visited in pc_offset
      // order, which is always the case since they are added to
      // deoptimization_exits_ in that order, and the optional sort operation
      // above preserves that order.
      if (exit->kind() == DeoptimizeKind::kLazy) {
        int trampoline_pc = exit->label()->pos();
        last_updated = safepoints()->UpdateDeoptimizationInfo(
            exit->pc_offset(), trampoline_pc, last_updated,
            exit->deoptimization_id());
      }
    }
  }

  offsets_info_.pools = masm()->pc_offset();
  // TODO(jgruber): Move all inlined metadata generation into a new,
  // architecture-independent version of FinishCode. Currently, this includes
  // the safepoint table, handler table, constant pool, and code comments, in
  // that order.
  FinishCode();

  offsets_info_.jump_tables = masm()->pc_offset();
  // Emit the jump tables.
  if (jump_tables_) {
    masm()->Align(kSystemPointerSize);
    for (JumpTable* table = jump_tables_; table; table = table->next()) {
      masm()->bind(table->label());
      AssembleJumpTable(table->targets(), table->target_count());
    }
  }

  // The LinuxPerfJitLogger logs code up until here, excluding the safepoint
  // table. Resolve the unwinding info now so it is aware of the same code
  // size as reported by perf.
  unwinding_info_writer_.Finish(masm()->pc_offset());

  // Final alignment before starting on the metadata section.
  masm()->Align(InstructionStream::kMetadataAlignment);

  safepoints()->Emit(masm(), frame()->GetTotalFrameSlotCount());

  // Emit the exception handler table.
  if (!handlers_.empty()) {
    handler_table_offset_ = HandlerTable::EmitReturnTableStart(masm());
    for (size_t i = 0; i < handlers_.size(); ++i) {
      int pos = handlers_[i].handler != nullptr ? handlers_[i].handler->pos()
                                                : HandlerTable::kLazyDeopt;
      HandlerTable::EmitReturnEntry(masm(), handlers_[i].pc_offset, pos);
    }
  }

  masm()->MaybeEmitOutOfLineConstantPool();
  masm()->FinalizeJumpOptimizationInfo();

  result_ = kSuccess;
}

#ifndef V8_TARGET_ARCH_X64
void CodeGenerator::AssembleArchBinarySearchSwitchRange(
    Register input, RpoNumber def_block, std::pair<int32_t, Label*>* begin,
    std::pair<int32_t, Label*>* end) {
  if (end - begin < kBinarySearchSwitchMinimalCases) {
    while (begin != end) {
      masm()->JumpIfEqual(input, begin->first, begin->second);
      ++begin;
    }
    AssembleArchJumpRegardlessOfAssemblyOrder(def_block);
    return;
  }
  auto middle = begin + (end - begin) / 2;
  Label less_label;
  masm()->JumpIfLessThan(input, middle->first, &less_label);
  AssembleArchBinarySearchSwitchRange(input, def_block, middle, end);
  masm()->bind(&less_label);
  AssembleArchBinarySearchSwitchRange(input, def_block, begin, middle);
}
#endif  // V8_TARGET_ARCH_X64

void CodeGenerator::AssembleArchJump(RpoNumber target) {
  if (!IsNextInAssemblyOrder(target))
    AssembleArchJumpRegardlessOfAssemblyOrder(target);
}

base::OwnedVector<uint8_t> CodeGenerator::GetSourcePositionTable() {
  return source_position_table_builder_.ToSourcePositionTableVector();
}

base::OwnedVector<uint8_t> CodeGenerator::GetProtectedInstructionsData() {
#if V8_ENABLE_WEBASSEMBLY
  return base::OwnedVector<uint8_t>::Of(
      base::Vector<uint8_t>::cast(base::VectorOf(protected_instructions_)));
#else
  return {};
#endif  // V8_ENABLE_WEBASSEMBLY
}

MaybeHandle<Code> CodeGenerator::FinalizeCode() {
  if (result_ != kSuccess) {
    masm()->AbortedCodeGeneration();
    return {};
  }

  // Allocate the source position table.
  Handle<TrustedByteArray> source_positions =
      source_position_table_builder_.ToSourcePositionTable(isolate());

  // Allocate and install the code.
  CodeDesc desc;
  masm()->GetCode(isolate()->main_thread_local_isolate(), &desc, safepoints(),
                  handler_table_offset_);

#if defined(V8_OS_WIN64)
  if (Builtins::IsBuiltinId(info_->builtin())) {
    isolate_->SetBuiltinUnwindData(info_->builtin(), masm()->GetUnwindInfo());
  }
#endif  // V8_OS_WIN64

  if (unwinding_info_writer_.eh_frame_writer()) {
    unwinding_info_writer_.eh_frame_writer()->GetEhFrame(&desc);
  }

  Factory::CodeBuilder builder(isolate(), desc, info()->code_kind());
  builder.set_builtin(info()->builtin())
      .set_inlined_bytecode_size(info()->inlined_bytecode_size())
      .set_parameter_count(parameter_count_)
      .set_source_position_table(source_positions)
      .set_is_turbofanned()
      .set_stack_slots(frame()->GetTotalFrameSlotCount())
      .set_profiler_data(info()->profiler_data())
      .set_osr_offset(info()->osr_offset());

  if (CodeKindUsesDeoptimizationData(info()->code_kind())) {
    builder.set_deoptimization_data(GenerateDeoptimizationData());
    DCHECK(info()->has_bytecode_array() ||
           info()->code_kind() == CodeKind::WASM_FUNCTION);
  }

  MaybeHandle<Code> maybe_code = builder.TryBuild();

  Handle<Code> code;
  if (!maybe_code.ToHandle(&code)) {
    masm()->AbortedCodeGeneration();
    return {};
  }

  LOG_CODE_EVENT(isolate(), CodeLinePosInfoRecordEvent(
                                code->instruction_start(), *source_positions,
                                JitCodeEvent::JIT_CODE));

  return code;
}

bool CodeGenerator::IsNextInAssemblyOrder(RpoNumber block) const {
  return instructions()
      ->InstructionBlockAt(current_block_)
      ->ao_number()
      .IsNext(instructions()->InstructionBlockAt(block)->ao_number());
}

void CodeGenerator::RecordSafepoint(ReferenceMap* references, int pc_offset) {
  auto safepoint = safepoints()->DefineSafepoint(masm(), pc_offset);

  for (int tagged : frame()->tagged_slots()) {
    safepoint.DefineTaggedStackSlot(tagged);
  }

  int frame_header_offset = frame()->GetFixedSlotCount();
  for (const InstructionOperand& operand : references->reference_operands()) {
    if (operand.IsStackSlot()) {
      int index = LocationOperand::cast(operand).index();
      DCHECK_LE(0, index);
      // We might index values in the fixed part of the frame (i.e. the
      // closure pointer or the context pointer); these are not spill slots
      // and therefore don't work with the SafepointTable currently, but
      // we also don't need to worry about them, since the GC has special
      // knowledge about those fields anyway.
      if (index < frame_header_offset) continue;
      safepoint.DefineTaggedStackSlot(index);
    }
  }
}

bool CodeGenerator::IsMaterializableFromRoot(Handle<HeapObject> object,
                                             RootIndex* index_return) {
  const CallDescriptor* incoming_descriptor =
      linkage()->GetIncomingDescriptor();
  if (incoming_descriptor->flags() & CallDescriptor::kCanUseRoots) {
    return isolate()->roots_table().IsRootHandle(object, index_return) &&
           RootsTable::IsImmortalImmovable(*index_return);
  }
  return false;
}

CodeGenerator::CodeGenResult CodeGenerator::AssembleBlock(
    const InstructionBlock* block) {
  if (block->IsHandler()) {
    masm()->ExceptionHandler();
  }
  for (int i = block->code_start(); i < block->code_end(); ++i) {
    CodeGenResult result = AssembleInstruction(i, block);
    if (result != kSuccess) return result;
  }
  return kSuccess;
}

bool CodeGenerator::IsValidPush(InstructionOperand source,
                                CodeGenerator::PushTypeFlags push_type) {
  if (source.IsImmediate() &&
      ((push_type & CodeGenerator::kImmediatePush) != 0)) {
    return true;
  }
  if (source.IsRegister() &&
      ((push_type & CodeGenerator::kRegisterPush) != 0)) {
    return true;
  }
  if (source.IsStackSlot() &&
      ((push_type & CodeGenerator::kStackSlotPush) != 0)) {
    return true;
  }
  return false;
}

void CodeGenerator::GetPushCompatibleMoves(Instruction* instr,
                                           PushTypeFlags push_type,
                                           ZoneVector<MoveOperands*>* pushes) {
  static constexpr int first_push_compatible_index =
      kReturnAddressStackSlotCount;
  pushes->clear();
  for (int i = Instruction::FIRST_GAP_POSITION;
       i <= Instruction::LAST_GAP_POSITION; ++i) {
    Instruction::GapPosition inner_pos =
        static_cast<Instruction::GapPosition>(i);
    ParallelMove* parallel_move = instr->GetParallelMove(inner_pos);
    if (parallel_move != nullptr) {
      for (auto move : *parallel_move) {
        InstructionOperand source = move->source();
        InstructionOperand destination = move->destination();
        // If there are any moves from slots that will be overridden by pushes,
        // then the full gap resolver must be used since optimization with
        // pushes don't participate in the parallel move and might clobber
        // values needed for the gap resolve.
        if (source.IsAnyStackSlot() && LocationOperand::cast(source).index() >=
                                           first_push_compatible_index) {
          pushes->clear();
          return;
        }
        // TODO(danno): Right now, only consider moves from the FIRST gap for
        // pushes. Theoretically, we could extract pushes for both gaps (there
        // are cases where this happens), but the logic for that would also have
        // to check to make sure that non-memory inputs to the pushes from the
        // LAST gap don't get clobbered in the FIRST gap.
        if (i == Instruction::FIRST_GAP_POSITION) {
          if (destination.IsStackSlot() &&
              LocationOperand::cast(destination).index() >=
                  first_push_compatible_index) {
            int index = LocationOperand::cast(destination).index();
            if (IsValidPush(source, push_type)) {
              if (index >= static_cast<int>(pushes->size())) {
                pushes->resize(index + 1);
              }
              (*pushes)[index] = move;
            }
          }
        }
      }
    }
  }

  // For now, only support a set of continuous pushes at the end of the list.
  size_t push_count_upper_bound = pushes->size();
  size_t push_begin = push_count_upper_bound;
  for (auto move : base::Reversed(*pushes)) {
    if (move == nullptr) break;
    push_begin--;
  }
  size_t push_count = pushes->size() - push_begin;
  std::copy(pushes->begin() + push_begin,
            pushes->begin() + push_begin + push_count, pushes->begin());
  pushes->resize(push_count);
}

CodeGenerator::MoveType::Type CodeGenerator::MoveType::InferMove(
    InstructionOperand* source, InstructionOperand* destination) {
  if (source->IsConstant()) {
    if (destination->IsAnyRegister()) {
      return MoveType::kConstantToRegister;
    } else {
      DCHECK(destination->IsAnyStackSlot());
      return MoveType::kConstantToStack;
    }
  }
  DCHECK(LocationOperand::cast(source)->IsCompatible(
      LocationOperand::cast(destination)));
  if (source->IsAnyRegister()) {
    if (destination->IsAnyRegister()) {
      return MoveType::kRegisterToRegister;
    } else {
      DCHECK(destination->IsAnyStackSlot());
      return MoveType::kRegisterToStack;
    }
  } else {
    DCHECK(source->IsAnyStackSlot());
    if (destination->IsAnyRegister()) {
      return MoveType::kStackToRegister;
    } else {
      DCHECK(destination->IsAnyStackSlot());
      return MoveType::kStackToStack;
    }
  }
}

CodeGenerator::MoveType::Type CodeGenerator::MoveType::InferSwap(
    InstructionOperand* source, InstructionOperand* destination) {
  DCHECK(LocationOperand::cast(source)->IsCompatible(
      LocationOperand::cast(destination)));
  if (source->IsAnyRegister()) {
    if (destination->IsAnyRegister()) {
      return MoveType::kRegisterToRegister;
    } else {
      DCHECK(destination->IsAnyStackSlot());
      return MoveType::kRegisterToStack;
    }
  } else {
    DCHECK(source->IsAnyStackSlot());
    DCHECK(destination->IsAnyStackSlot());
    return MoveType::kStackToStack;
  }
}

RpoNumber CodeGenerator::ComputeBranchInfo(BranchInfo* branch,
                                           FlagsCondition condition,
                                           Instruction* instr) {
  // Assemble a branch after this instruction.
  InstructionOperandConverter i(this, instr);
  RpoNumber true_rpo =
      i.InputRpo(instr->InputCount() - kBranchEndOffsetOfTrueBlock);
  RpoNumber false_rpo =
      i.InputRpo(instr->InputCount() - kBranchEndOffsetOfFalseBlock);

  if (true_rpo == false_rpo) {
    return true_rpo;
  }
  if (IsNextInAssemblyOrder(true_rpo) || instructions()
                                             ->InstructionBlockAt(false_rpo)
                                             ->IsLoopHeaderInAssemblyOrder()) {
    // true block is next, can fall through if condition negated.
    // false block is loop header, can save one jump if condition negated.
    std::swap(true_rpo, false_rpo);
    condition = NegateFlagsCondition(condition);
  }
  branch->condition = condition;
  branch->true_label = GetLabel(true_rpo);
  branch->false_label = GetLabel(false_rpo);
  branch->fallthru = IsNextInAssemblyOrder(false_rpo);
  return RpoNumber::Invalid();
}

CodeGenerator::CodeGenResult CodeGenerator::AssembleInstruction(
    int instruction_index, const InstructionBlock* block) {
  Instruction* instr = instructions()->InstructionAt(instruction_index);
  if (info()->trace_turbo_json()) {
    instr_starts_[instruction_index].gap_pc_offset = masm()->pc_offset();
  }
  int first_unused_stack_slot;
  FlagsMode mode = FlagsModeField::decode(instr->opcode());
  if (mode != kFlags_trap) {
    AssembleSourcePosition(instr);
  }
  bool adjust_stack =
      GetSlotAboveSPBeforeTailCall(instr, &first_unused_stack_slot);
  if (adjust_stack) AssembleTailCallBeforeGap(instr, first_unused_stack_slot);
  if (instr->opcode() == kArchNop && block->successors().empty() &&
      block->code_end() - block->code_start() == 1) {
    // When the frame-less dummy end block in Turbofan contains a Phi node,
    // don't attempt to access spill slots.
    // TODO(dmercadier): When the switch to Turboshaft is complete, this
    // will no longer be required.
  } else {
    AssembleGaps(instr);
  }
  if (adjust_stack) AssembleTailCallAfterGap(instr, first_unused_stack_slot);
  DCHECK_IMPLIES(
      block->must_deconstruct_frame(),
      instr != instructions()->InstructionAt(block->last_instruction_index()) ||
          instr->IsRet() || instr->IsJump());
  if (instr->IsJump() && block->must_deconstruct_frame()) {
    AssembleDeconstructFrame();
  }
  if (info()->trace_turbo_json()) {
    instr_starts_[instruction_index].arch_instr_pc_offset = masm()->pc_offset();
  }
  // Assemble architecture-specific code for the instruction.
  CodeGenResult result = AssembleArchInstruction(instr);
  if (result != kSuccess) return result;

  if (info()->trace_turbo_json()) {
    instr_starts_[instruction_index].condition_pc_offset = masm()->pc_offset();
  }

  FlagsCondition condition = FlagsConditionField::decode(instr->opcode());
  switch (mode) {
    case kFlags_branch:
    case kFlags_conditional_branch: {
      if (mode == kFlags_conditional_branch) {
        InstructionOperandConverter i(this, instr);
        condition = static_cast<FlagsCondition>(
            i.ToConstant(instr->InputAt(instr->InputCount() -
                                        kConditionalBranchEndOffsetOfCondition))
                .ToInt64());
      }
      BranchInfo branch;
      RpoNumber target = ComputeBranchInfo(&branch, condition, instr);
      if (target.IsValid()) {
        // redundant branch.
        if (!IsNextInAssemblyOrder(target)) {
          AssembleArchJump(target);
        }
        return kSuccess;
      }
      if (mode == kFlags_branch) {
        // Assemble architecture-specific branch.
        AssembleArchBranch(instr, &branch);
      } else {
        AssembleArchConditionalBranch(instr, &branch);
      }
      break;
    }
    case kFlags_deoptimize: {
      // Assemble a conditional eager deoptimization after this instruction.
      InstructionOperandConverter i(this, instr);
      size_t frame_state_offset =
          DeoptFrameStateOffsetField::decode(instr->opcode());
      size_t immediate_args_count =
          DeoptImmedArgsCountField::decode(instr->opcode());
      DeoptimizationExit* const exit = AddDeoptimizationExit(
          instr, frame_state_offset, immediate_args_count);
      BranchInfo branch;
      branch.condition = condition;
      branch.true_label = exit->label();
      branch.false_label = exit->continue_label();
      branch.fallthru = true;
      AssembleArchDeoptBranch(instr, &branch);
      masm()->bind(exit->continue_label());
      break;
    }
    case kFlags_set: {
      // Assemble a boolean materialization after this instruction.
      AssembleArchBoolean(instr, condition);
      break;
    }
    case kFlags_conditional_set: {
      // Assemble a conditional boolean materialization after this instruction.
      AssembleArchConditionalBoolean(instr);
      break;
    }
    case kFlags_select: {
      AssembleArchSelect(instr, condition);
      break;
    }
    case kFlags_trap: {
#if V8_ENABLE_WEBASSEMBLY
      AssembleArchTrap(instr, condition);
      break;
#else
      UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    case kFlags_none: {
      break;
    }
  }

  return kSuccess;
}

void CodeGenerator::AssembleSourcePosition(Instruction* instr) {
  SourcePosition source_position = SourcePosition::Unknown();
  if (instr->IsNop() && instr->AreMovesRedundant()) return;
  if (!instructions()->GetSourcePosition(instr, &source_position)) return;
  AssembleSourcePosition(source_position);
}

void CodeGenerator::AssembleSourcePosition(SourcePosition source_position) {
  if (source_position == current_source_position_) return;
  current_source_position_ = source_position;
  if (!source_position.IsKnown()) return;
  source_position_table_builder_.AddPosition(masm()->pc_offset(),
                                             source_position, false);
  if (v8_flags.code_comments) {
    OptimizedCompilationInfo* info = this->info();
    if (!info->IsOptimizing()) {
#if V8_ENABLE_WEBASSEMBLY
      if (!info->IsWasm()) return;
#else
      return;
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    std::ostringstream buffer;
    buffer << "-- ";
    // Turbolizer only needs the source position, as it can reconstruct
    // the inlining stack from other information.
    if (info->trace_turbo_json() || !masm()->isolate() ||
        masm()->isolate()->concurrent_recompilation_enabled()) {
      buffer << source_position;
    } else {
      AllowGarbageCollection allocation;
      AllowHandleAllocation handles;
      AllowHandleDereference deref;
      buffer << source_position.InliningStack(masm()->isolate(), info);
    }
    buffer << " --";
    masm()->RecordComment(buffer.str().c_str(), SourceLocation());
  }
}

bool CodeGenerator::GetSlotAboveSPBeforeTailCall(Instruction* instr,
                                                 int* slot) {
  if (instr->IsTailCall()) {
    InstructionOperandConverter g(this, instr);
    *slot = g.InputInt32(instr->InputCount() - 1);
    return true;
  } else {
    return false;
  }
}

StubCallMode CodeGenerator::DetermineStubCallMode() const {
#if V8_ENABLE_WEBASSEMBLY
  CodeKind code_kind = info()->code_kind();
  if (code_kind == CodeKind::WASM_FUNCTION ||
      code_kind == CodeKind::WASM_TO_CAPI_FUNCTION ||
      code_kind == CodeKind::WASM_TO_JS_FUNCTION) {
    return StubCallMode::kCallWasmRuntimeStub;
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return StubCallMode::kCallCodeObject;
}

void CodeGenerator::AssembleGaps(Instruction* instr) {
  for (int i = Instruction::FIRST_GAP_POSITION;
       i <= Instruction::LAST_GAP_POSITION; i++) {
    Instruction::GapPosition inner_pos =
        static_cast<Instruction::GapPosition>(i);
    ParallelMove* move = instr->GetParallelMove(inner_pos);
    if (move != nullptr) resolver()->Resolve(move);
  }
}

namespace {

Handle<TrustedPodArray<InliningPosition>> CreateInliningPositions(
    OptimizedCompilationInfo* info, Isolate* isolate) {
  const OptimizedCompilationInfo::InlinedFunctionList& inlined_functions =
      info->inlined_functions();
  Handle<TrustedPodArray<InliningPosition>> inl_positions =
      TrustedPodArray<InliningPosition>::New(
          isolate, static_cast<int>(inlined_functions.size()));
  for (size_t i = 0; i < inlined_functions.size(); ++i) {
    inl_positions->set(static_cast<int>(i), inlined_functions[i].position);
  }
  return inl_positions;
}

}  // namespace

Handle<DeoptimizationData> CodeGenerator::GenerateDeoptimizationData() {
  OptimizedCompilationInfo* info = this->info();
  int deopt_count = static_cast<int>(deoptimization_exits_.size());
  if (deopt_count == 0 && !info->is_osr()) {
    return DeoptimizationData::Empty(isolate());
  }
  Handle<DeoptimizationData> data =
      DeoptimizationData::New(isolate(), deopt_count);

  DirectHandle<DeoptimizationFrameTranslation> translation_array =
      translations_.ToFrameTranslation(
          isolate()->main_thread_local_isolate()->factory());

  data->SetFrameTranslation(*translation_array);
  data->SetInlinedFunctionCount(
      Smi::FromInt(static_cast<int>(inlined_function_count_)));
  data->SetOptimizationId(Smi::FromInt(info->optimization_id()));

  data->SetDeoptExitStart(Smi::FromInt(deopt_exit_start_offset_));
  data->SetEagerDeoptCount(Smi::FromInt(eager_deopt_count_));
  data->SetLazyDeoptCount(Smi::FromInt(lazy_deopt_count_));

  if (info->has_shared_info()) {
    DirectHandle<SharedFunctionInfoWrapper> sfi_wrapper =
        isolate()->factory()->NewSharedFunctionInfoWrapper(info->shared_info());
    data->SetSharedFunctionInfoWrapper(*sfi_wrapper);
  } else {
    data->SetSharedFunctionInfoWrapper(Smi::zero());
  }

  DirectHandle<DeoptimizationLiteralArray> literals =
      isolate()->factory()->NewDeoptimizationLiteralArray(
          static_cast<int>(deoptimization_literals_.size()));
  for (unsigned i = 0; i < deoptimization_literals_.size(); i++) {
    Handle<Object> object = deoptimization_literals_[i].Reify(isolate());
    CHECK(!object.is_null());
    literals->set(i, *object);
  }
  data->SetLiteralArray(*literals);

  DirectHandle<TrustedPodArray<InliningPosition>> inl_pos =
      CreateInliningPositions(info, isolate());
  data->SetInliningPositions(*inl_pos);

  if (info->is_osr()) {
    DCHECK_LE(0, osr_pc_offset_);
    data->SetOsrBytecodeOffset(Smi::FromInt(info_->osr_offset().ToInt()));
    data->SetOsrPcOffset(Smi::FromInt(osr_pc_offset_));
  } else {
    BytecodeOffset osr_offset = BytecodeOffset::None();
    data->SetOsrBytecodeOffset(Smi::FromInt(osr_offset.ToInt()));
    data->SetOsrPcOffset(Smi::FromInt(-1));
  }

  // Populate deoptimization entries.
  for (int i = 0; i < deopt_count; i++) {
    DeoptimizationExit* deoptimization_exit = deoptimization_exits_[i];
    CHECK_NOT_NULL(deoptimization_exit);
    DCHECK_EQ(i, deoptimization_exit->deoptimization_id());
    data->SetBytecodeOffset(i, deoptimization_exit->bailout_id());
    data->SetTranslationIndex(
        i, Smi::FromInt(deoptimization_exit->translation_id()));
    data->SetPc(i, Smi::FromInt(deoptimization_exit->pc_offset()));
#ifdef DEBUG
    data->SetNodeId(i, Smi::FromInt(deoptimization_exit->node_id()));
#endif  // DEBUG
  }

#ifdef DEBUG
  data->Verify(info->bytecode_array());
#endif  // DEBUG
  return data;
}

#if V8_ENABLE_WEBASSEMBLY
base::OwnedVector<uint8_t> CodeGenerator::GenerateWasmDeoptimizationData() {
  int deopt_count = static_cast<int>(deoptimization_exits_.size());
  if (deopt_count == 0) {
    return {};
  }
  // Lazy deopts are not supported in wasm.
  DCHECK_EQ(lazy_deopt_count_, 0);
  // Wasm doesn't use the JS inlining handling via deopt info.
  // TODO(mliedtke): Re-evaluate if this would offer benefits.
  DCHECK_EQ(inlined_function_count_, 0);

  auto deopt_entries =
      base::OwnedVector<wasm::WasmDeoptEntry>::New(deopt_count);
  // Populate deoptimization entries.
  for (int i = 0; i < deopt_count; i++) {
    const DeoptimizationExit* deoptimization_exit = deoptimization_exits_[i];
    CHECK_NOT_NULL(deoptimization_exit);
    DCHECK_EQ(i, deoptimization_exit->deoptimization_id());
    deopt_entries[i] = {deoptimization_exit->bailout_id(),
                        deoptimization_exit->translation_id()};
  }

  base::Vector<const uint8_t> frame_translations =
      translations_.ToFrameTranslationWasm();
  base::OwnedVector<uint8_t> result = wasm::WasmDeoptDataProcessor::Serialize(
      deopt_exit_start_offset_, eager_deopt_count_, frame_translations,
      base::VectorOf(deopt_entries), deoptimization_literals_);
#if DEBUG
  // Verify that the serialized data can be deserialized.
  wasm::WasmDeoptView view(base::VectorOf(result));
  wasm::WasmDeoptData data = view.GetDeoptData();
  DCHECK_EQ(data.deopt_exit_start_offset, deopt_exit_start_offset_);
  DCHECK_EQ(data.deopt_literals_size, deoptimization_literals_.size());
  DCHECK_EQ(data.eager_deopt_count, eager_deopt_count_);
  DCHECK_EQ(data.entry_count, deoptimization_exits_.size());
  DCHECK_EQ(data.translation_array_size, frame_translations.size());
  for (int i = 0; i < deopt_count; i++) {
    const DeoptimizationExit* exit = deoptimization_exits_[i];
    wasm::WasmDeoptEntry entry = view.GetDeoptEntry(i);
    DCHECK_EQ(exit->bailout_id(), entry.bytecode_offset);
    DCHECK_EQ(exit->translation_id(), entry.translation_index);
  }
  std::vector<DeoptimizationLiteral> literals =
      view.BuildDeoptimizationLiteralArray();
  DCHECK_EQ(literals.size(), deoptimization_literals_.size());
  for (size_t i = 0; i < deoptimization_literals_.size(); ++i) {
    DCHECK_EQ(literals[i], deoptimization_literals_[i]);
  }
#endif
  return result;
}
#endif  // V8_ENABLE_WEBASSEMBLY

Label* CodeGenerator::AddJumpTable(Label** targets, size_t target_count) {
  jump_tables_ = zone()->New<JumpTable>(jump_tables_, targets, target_count);
  return jump_tables_->label();
}

void CodeGenerator::RecordCallPosition(Instruction* instr) {
  const bool needs_frame_state =
      instr->HasCallDescriptorFlag(CallDescriptor::kNeedsFrameState);
  RecordSafepoint(instr->reference_map());

  if (instr->HasCallDescriptorFlag(CallDescriptor::kHasExceptionHandler)) {
    InstructionOperandConverter i(this, instr);
    Constant handler_input =
        i.ToConstant(instr->InputAt(instr->InputCount() - 1));
    if (handler_input.type() == Constant::Type::kRpoNumber) {
      RpoNumber handler_rpo = handler_input.ToRpoNumber();
      DCHECK(instructions()->InstructionBlockAt(handler_rpo)->IsHandler());
      handlers_.push_back(
          {GetLabel(handler_rpo), masm()->pc_offset_for_safepoint()});
    } else {
      // We should lazy deopt on throw.
      DCHECK_EQ(handler_input.ToInt32(), kLazyDeoptOnThrowSentinel);
      handlers_.push_back({nullptr, masm()->pc_offset_for_safepoint()});
    }
  }

  if (needs_frame_state) {
    RecordDeoptInfo(instr, masm()->pc_offset_for_safepoint());
  }
}

void CodeGenerator::RecordDeoptInfo(Instruction* instr, int pc_offset) {
  // If the frame state is present, it starts at argument 1 - after
  // the code address.
  size_t frame_state_offset = 1;
  FrameStateDescriptor* descriptor =
      GetDeoptimizationEntry(instr, frame_state_offset).descriptor();
  BuildTranslation(instr, pc_offset, frame_state_offset, 0,
                   descriptor->state_combine());
}

int CodeGenerator::DefineDeoptimizationLiteral(DeoptimizationLiteral literal) {
  literal.Validate();
  int result = static_cast<int>(deoptimization_literals_.size());
  for (unsigned i = 0; i < deoptimization_literals_.size(); ++i) {
    deoptimization_literals_[i].Validate();
    if (deoptimization_literals_[i] == literal) return i;
  }
  deoptimization_literals_.push_back(literal);
  return result;
}

DeoptimizationEntry const& CodeGenerator::GetDeoptimizationEntry(
    Instruction* instr, size_t frame_state_offset) {
  InstructionOperandConverter i(this, instr);
  int const state_id = i.InputInt32(frame_state_offset);
  return instructions()->GetDeoptimizationEntry(state_id);
}

void CodeGenerator::TranslateStateValueDescriptor(
    StateValueDescriptor* desc, StateValueList* nested,
    InstructionOperandIterator* iter) {
  if (desc->IsNested()) {
    translations_.BeginCapturedObject(static_cast<int>(nested->size()));
    for (auto field : *nested) {
      TranslateStateValueDescriptor(field.desc, field.nested, iter);
    }
  } else if (desc->IsArgumentsElements()) {
    translations_.ArgumentsElements(desc->arguments_type());
  } else if (desc->IsArgumentsLength()) {
    translations_.ArgumentsLength();
  } else if (desc->IsRestLength()) {
    translations_.RestLength();
  } else if (desc->IsDuplicate()) {
    translations_.DuplicateObject(static_cast<int>(desc->id()));
  } else if (desc->IsPlain()) {
    InstructionOperand* op = iter->Advance();
    AddTranslationForOperand(iter->instruction(), op, desc->type());
  } else {
    DCHECK(desc->IsOptimizedOut());
    translations_.StoreOptimizedOut();
  }
}

void CodeGenerator::TranslateFrameStateDescriptorOperands(
    FrameStateDescriptor* desc, InstructionOperandIterator* iter) {
  size_t index = 0;
  StateValueList* values = desc->GetStateValueDescriptors();
  for (StateValueList::iterator it = values->begin(); it != values->end();
       ++it, ++index) {
    TranslateStateValueDescriptor((*it).desc, (*it).nested, iter);
  }
  DCHECK_EQ(desc->GetSize(), index);
}

void CodeGenerator::BuildTranslationForFrameStateDescriptor(
    FrameStateDescriptor* descriptor, InstructionOperandIterator* iter,
    OutputFrameStateCombine state_combine) {
  // Outer-most state must be added to translation first.
  if (descriptor->outer_state() != nullptr) {
    BuildTranslationForFrameStateDescriptor(descriptor->outer_state(), iter,
                                            state_combine);
  }

  Handle<SharedFunctionInfo> shared_info;
  if (!descriptor->shared_info().ToHandle(&shared_info)) {
    if (!info()->has_shared_info()
#if V8_ENABLE_WEBASSEMBLY
        && descriptor->type() != compiler::FrameStateType::kLiftoffFunction
#endif
    ) {
      return;  // Stub with no SharedFunctionInfo.
    }
    shared_info = info()->shared_info();
  }

  const BytecodeOffset bailout_id = descriptor->bailout_id();

  const int shared_info_id =
#if V8_ENABLE_WEBASSEMBLY
      shared_info.is_null()
          ? DefineDeoptimizationLiteral(DeoptimizationLiteral(uint64_t{0}))
          : DefineDeoptimizationLiteral(DeoptimizationLiteral(shared_info));
  CHECK_IMPLIES(shared_info.is_null(), v8_flags.wasm_deopt);
#else
      DefineDeoptimizationLiteral(DeoptimizationLiteral(shared_info));
#endif

  const unsigned int height =
      static_cast<unsigned int>(descriptor->GetHeight());

  switch (descriptor->type()) {
    case FrameStateType::kUnoptimizedFunction: {
      int return_offset = 0;
      int return_count = 0;
      if (!state_combine.IsOutputIgnored()) {
        return_offset = static_cast<int>(state_combine.GetOffsetToPokeAt());
        return_count = static_cast<int>(iter->instruction()->OutputCount());
      }
      translations_.BeginInterpretedFrame(bailout_id, shared_info_id, height,
                                          return_offset, return_count);
      break;
    }
    case FrameStateType::kInlinedExtraArguments:
      translations_.BeginInlinedExtraArguments(shared_info_id, height);
      break;
    case FrameStateType::kConstructCreateStub:
      translations_.BeginConstructCreateStubFrame(shared_info_id, height);
      break;
    case FrameStateType::kConstructInvokeStub:
      translations_.BeginConstructInvokeStubFrame(shared_info_id);
      break;
    case FrameStateType::kBuiltinContinuation: {
      translations_.BeginBuiltinContinuationFrame(bailout_id, shared_info_id,
                                                  height);
      break;
    }
#if V8_ENABLE_WEBASSEMBLY
    case FrameStateType::kWasmInlinedIntoJS:
      translations_.BeginWasmInlinedIntoJSFrame(bailout_id, shared_info_id,
                                                height);
      break;
    case FrameStateType::kJSToWasmBuiltinContinuation: {
      const JSToWasmFrameStateDescriptor* js_to_wasm_descriptor =
          static_cast<const JSToWasmFrameStateDescriptor*>(descriptor);
      translations_.BeginJSToWasmBuiltinContinuationFrame(
          bailout_id, shared_info_id, height,
          js_to_wasm_descriptor->return_kind());
      break;
    }
    case FrameStateType::kLiftoffFunction:
      translations_.BeginLiftoffFrame(bailout_id, height,
                                      descriptor->GetWasmFunctionIndex());
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case FrameStateType::kJavaScriptBuiltinContinuation: {
      translations_.BeginJavaScriptBuiltinContinuationFrame(
          bailout_id, shared_info_id, height);
      break;
    }
    case FrameStateType::kJavaScriptBuiltinContinuationWithCatch: {
      translations_.BeginJavaScriptBuiltinContinuationWithCatchFrame(
          bailout_id, shared_info_id, height);
      break;
    }
  }

  TranslateFrameStateDescriptorOperands(descriptor, iter);
}

DeoptimizationExit* CodeGenerator::BuildTranslation(
    Instruction* instr, int pc_offset, size_t frame_state_offset,
    size_t immediate_args_count, OutputFrameStateCombine state_combine) {
  DeoptimizationEntry const& entry =
      GetDeoptimizationEntry(instr, frame_state_offset);
  FrameStateDescriptor* const descriptor = entry.descriptor();
  frame_state_offset++;

  const int translation_index = translations_.BeginTranslation(
      static_cast<int>(descriptor->GetFrameCount()),
      static_cast<int>(descriptor->GetJSFrameCount()),
      entry.feedback().IsValid());
  if (entry.feedback().IsValid()) {
    DeoptimizationLiteral literal =
        DeoptimizationLiteral(entry.feedback().vector);
    int literal_id = DefineDeoptimizationLiteral(literal);
    translations_.AddUpdateFeedback(literal_id, entry.feedback().slot.ToInt());
  }
  InstructionOperandIterator iter(instr, frame_state_offset);
  BuildTranslationForFrameStateDescriptor(descriptor, &iter, state_combine);

  DeoptimizationExit* const exit = zone()->New<DeoptimizationExit>(
      current_source_position_, descriptor->bailout_id(), translation_index,
      pc_offset, entry.kind(), entry.reason(),
#ifdef DEBUG
      entry.node_id());
#else   // DEBUG
      0);
#endif  // DEBUG
  if (immediate_args_count != 0) {
    auto immediate_args = zone()->New<ZoneVector<ImmediateOperand*>>(zone());
    InstructionOperandIterator imm_iter(
        instr, frame_state_offset - immediate_args_count - 1);
    for (size_t i = 0; i < immediate_args_count; i++) {
      immediate_args->emplace_back(ImmediateOperand::cast(imm_iter.Advance()));
    }
    exit->set_immediate_args(immediate_args);
  }

  deoptimization_exits_.push_back(exit);
  return exit;
}

void CodeGenerator::AddTranslationForOperand(Instruction* instr,
                                             InstructionOperand* op,
                                             MachineType type) {
  if (op->IsStackSlot()) {
    if (type.representation() == MachineRepresentation::kBit) {
      translations_.StoreBoolStackSlot(LocationOperand::cast(op)->index());
    } else if (type == MachineType::Int8() || type == MachineType::Int16() ||
               type == MachineType::Int32()) {
      translations_.StoreInt32StackSlot(LocationOperand::cast(op)->index());
    } else if (type == MachineType::Uint8() || type == MachineType::Uint16() ||
               type == MachineType::Uint32()) {
      translations_.StoreUint32StackSlot(LocationOperand::cast(op)->index());
    } else if (type == MachineType::Int64()) {
      translations_.StoreInt64StackSlot(LocationOperand::cast(op)->index());
    } else if (type == MachineType::SignedBigInt64()) {
      translations_.StoreSignedBigInt64StackSlot(
          LocationOperand::cast(op)->index());
    } else if (type == MachineType::UnsignedBigInt64()) {
      translations_.StoreUnsignedBigInt64StackSlot(
          LocationOperand::cast(op)->index());
    } else {
#if defined(V8_COMPRESS_POINTERS)
      CHECK(MachineRepresentation::kTagged == type.representation() ||
            MachineRepresentation::kCompressed == type.representation());
#else
      CHECK(MachineRepresentation::kTagged == type.representation());
#endif
      translations_.StoreStackSlot(LocationOperand::cast(op)->index());
    }
  } else if (op->IsFPStackSlot()) {
    switch (type.representation()) {
      case MachineRepresentation::kFloat32:
        translations_.StoreFloatStackSlot(LocationOperand::cast(op)->index());
        break;
      case MachineRepresentation::kFloat64:
        if (type.semantic() == MachineSemantic::kHoleyFloat64) {
          translations_.StoreHoleyDoubleStackSlot(
              LocationOperand::cast(op)->index());
        } else {
          translations_.StoreDoubleStackSlot(
              LocationOperand::cast(op)->index());
        }
        break;
      case MachineRepresentation::kSimd128:
        translations_.StoreSimd128StackSlot(LocationOperand::cast(op)->index());
        break;
      default:
        UNREACHABLE();
    }
  } else if (op->IsRegister()) {
    InstructionOperandConverter converter(this, instr);
    if (type.representation() == MachineRepresentation::kBit) {
      translations_.StoreBoolRegister(converter.ToRegister(op));
    } else if (type == MachineType::Int8() || type == MachineType::Int16() ||
               type == MachineType::Int32()) {
      translations_.StoreInt32Register(converter.ToRegister(op));
    } else if (type == MachineType::Uint8() || type == MachineType::Uint16() ||
               type == MachineType::Uint32()) {
      translations_.StoreUint32Register(converter.ToRegister(op));
    } else if (type == MachineType::Int64()) {
      translations_.StoreInt64Register(converter.ToRegister(op));
    } else if (type == MachineType::SignedBigInt64()) {
      translations_.StoreSignedBigInt64Register(converter.ToRegister(op));
    } else if (type == MachineType::UnsignedBigInt64()) {
      translations_.StoreUnsignedBigInt64Register(converter.ToRegister(op));
    } else {
#if defined(V8_COMPRESS_POINTERS)
      CHECK(MachineRepresentation::kTagged == type.representation() ||
            MachineRepresentation::kCompressed == type.representation());
#else
      CHECK(MachineRepresentation::kTagged == type.representation());
#endif
      translations_.StoreRegister(converter.ToRegister(op));
    }
  } else if (op->IsFPRegister()) {
    InstructionOperandConverter converter(this, instr);
    switch (type.representation()) {
      case MachineRepresentation::kFloat32:
        translations_.StoreFloatRegister(converter.ToFloatRegister(op));
        break;
      case MachineRepresentation::kFloat64:
        if (type.semantic() == MachineSemantic::kHoleyFloat64) {
          translations_.StoreHoleyDoubleRegister(
              converter.ToDoubleRegister(op));
        } else {
          translations_.StoreDoubleRegister(converter.ToDoubleRegister(op));
        }
        break;
      case MachineRepresentation::kSimd128:
        translations_.StoreSimd128Register(converter.ToSimd128Register(op));
        break;
      default:
        UNREACHABLE();
    }
  } else {
    CHECK(op->IsImmediate());
    InstructionOperandConverter converter(this, instr);
    Constant constant = converter.ToConstant(op);
    DeoptimizationLiteral literal;

#if V8_ENABLE_WEBASSEMBLY
    if (info_->IsWasm() && v8_flags.wasm_deopt) {
      switch (type.representation()) {
        case MachineRepresentation::kWord32:
          literal = DeoptimizationLiteral(constant.ToInt32());
          break;
        case MachineRepresentation::kWord64:
          literal = DeoptimizationLiteral(constant.ToInt64());
          break;
        case MachineRepresentation::kFloat32:
          literal = DeoptimizationLiteral(constant.ToFloat32Safe());
          break;
        case MachineRepresentation::kFloat64:
          literal = DeoptimizationLiteral(Float64(constant.ToFloat64()));
          break;
        case MachineRepresentation::kTagged: {
          DCHECK(!PointerCompressionIsEnabled() ||
                 base::IsInRange(constant.ToInt64(), 0u, UINT32_MAX));
          Tagged<Smi> smi(static_cast<Address>(constant.ToInt64()));
          DCHECK(IsSmi(smi));
          literal = DeoptimizationLiteral(smi);
          break;
        }
        default:
          UNIMPLEMENTED();
      }
      int literal_id = DefineDeoptimizationLiteral(literal);
      translations_.StoreLiteral(literal_id);
      return;
    }
#endif

    switch (constant.type()) {
      case Constant::kInt32:
        if (type.representation() == MachineRepresentation::kTagged) {
          // When pointers are 4 bytes, we can use int32 constants to represent
          // Smis.
          DCHECK_EQ(4, kSystemPointerSize);
          Tagged<Smi> smi(static_cast<Address>(constant.ToInt32()));
          DCHECK(IsSmi(smi));
          literal = DeoptimizationLiteral(static_cast<double>(smi.value()));
        } else if (type.representation() == MachineRepresentation::kBit) {
          if (constant.ToInt32() == 0) {
            literal =
                DeoptimizationLiteral(isolate()->factory()->false_value());
          } else {
            DCHECK_EQ(1, constant.ToInt32());
            literal = DeoptimizationLiteral(isolate()->factory()->true_value());
          }
        } else {
          DCHECK(type == MachineType::Int32() ||
                 type == MachineType::Uint32() ||
                 type.representation() == MachineRepresentation::kWord32 ||
                 type.representation() == MachineRepresentation::kNone);
          DCHECK(type.representation() != MachineRepresentation::kNone ||
                 constant.ToInt32() == FrameStateDescriptor::kImpossibleValue);
          if (type == MachineType::Uint32()) {
            literal = DeoptimizationLiteral(
                static_cast<double>(static_cast<uint32_t>(constant.ToInt32())));
          } else {
            literal =
                DeoptimizationLiteral(static_cast<double>(constant.ToInt32()));
          }
        }
        break;
      case Constant::kInt64:
        DCHECK_EQ(8, kSystemPointerSize);
        if (type == MachineType::SignedBigInt64()) {
          literal = DeoptimizationLiteral(constant.ToInt64());
        } else if (type == MachineType::UnsignedBigInt64()) {
          literal =
              DeoptimizationLiteral(static_cast<uint64_t>(constant.ToInt64()));
        } else if (type.representation() == MachineRepresentation::kWord64) {
          // TODO(nicohartmann@, chromium:41497374): Disabling this CHECK
          // because we can see cases where this is violated in unreachable
          // code. We should re-enable once we have an idea on how to prevent
          // this from happening.
          // CHECK_EQ(
          //     constant.ToInt64(),
          //     static_cast<int64_t>(static_cast<double>(constant.ToInt64())));
          literal =
              DeoptimizationLiteral(static_cast<double>(constant.ToInt64()));
        } else {
          // When pointers are 8 bytes, we can use int64 constants to represent
          // Smis.
          DCHECK_EQ(MachineRepresentation::kTagged, type.representation());
          Tagged<Smi> smi(static_cast<Address>(constant.ToInt64()));
          DCHECK(IsSmi(smi));
          literal = DeoptimizationLiteral(static_cast<double>(smi.value()));
        }
        break;
      case Constant::kFloat32:
        DCHECK(type.representation() == MachineRepresentation::kFloat32 ||
               type.representation() == MachineRepresentation::kTagged);
        literal =
            DeoptimizationLiteral(static_cast<double>(constant.ToFloat32()));
        break;
      case Constant::kFloat64:
        DCHECK(type.representation() == MachineRepresentation::kFloat64 ||
               type.representation() == MachineRepresentation::kTagged);
        if (type == MachineType::HoleyFloat64() &&
            constant.ToFloat64().AsUint64() == kHoleNanInt64) {
          literal = DeoptimizationLiteral::HoleNaN();
        } else {
          literal = DeoptimizationLiteral(constant.ToFloat64().value());
        }
        break;
      case Constant::kHeapObject:
        DCHECK_EQ(MachineRepresentation::kTagged, type.representation());
        literal = DeoptimizationLiteral(constant.ToHeapObject());
        break;
      case Constant::kCompressedHeapObject:
        DCHECK_EQ(MachineType::AnyTagged(), type);
        literal = DeoptimizationLiteral(constant.ToHeapObject());
        break;
      default:
        UNREACHABLE();
    }
    if (literal.object().equals(info()->closure()) &&
        info()->function_context_specializing()) {
      translations_.StoreJSFrameFunction();
    } else {
      int literal_id = DefineDeoptimizationLiteral(literal);
      translations_.StoreLiteral(literal_id);
    }
  }
}

DeoptimizationExit* CodeGenerator::AddDeoptimizationExit(
    Instruction* instr, size_t frame_state_offset,
    size_t immediate_args_count) {
  return BuildTranslation(instr, -1, frame_state_offset, immediate_args_count,
                          OutputFrameStateCombine::Ignore());
}

OutOfLineCode::OutOfLineCode(CodeGenerator* gen)
    : frame_(gen->frame()), masm_(gen->masm()), next_(gen->ools_) {
  gen->ools_ = this;
}

OutOfLineCode::~OutOfLineCode() = default;

}  // namespace compiler
}  // namespace internal
}  // namespace v8

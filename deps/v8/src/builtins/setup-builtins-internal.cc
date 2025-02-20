// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include "src/builtins/builtins-inl.h"
#include "src/builtins/profile-data-reader.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/compiler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/reloc-info-inl.h"
#include "src/common/globals.h"
#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"
#include "src/compiler/code-assembler.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/turboshaft/builtin-compiler.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/init/setup-isolate.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter-generator.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-builtin-list.h"
#endif

namespace v8 {
namespace internal {

// Forward declarations for C++ builtins.
#define FORWARD_DECLARE(Name, Argc) \
  Address Builtin_##Name(int argc, Address* args, Isolate* isolate);
BUILTIN_LIST_C(FORWARD_DECLARE)
#undef FORWARD_DECLARE

namespace {

using BuiltinCompilationScheduler =
    compiler::CodeAssembler::BuiltinCompilationScheduler;

const int kBufferSize = 128 * KB;

AssemblerOptions BuiltinAssemblerOptions(Isolate* isolate, Builtin builtin) {
  AssemblerOptions options = AssemblerOptions::Default(isolate);
  CHECK(!options.isolate_independent_code);
  CHECK(!options.collect_win64_unwind_info);

#if V8_ENABLE_WEBASSEMBLY
  if (wasm::BuiltinLookup::IsWasmBuiltinId(builtin) ||
      builtin == Builtin::kJSToWasmWrapper ||
      builtin == Builtin::kJSToWasmHandleReturns ||
      builtin == Builtin::kWasmToJsWrapperCSA) {
    options.is_wasm = true;
  }
#endif
  if (!isolate->IsGeneratingEmbeddedBuiltins()) {
    return options;
  }

  const base::AddressRegion& code_region = isolate->heap()->code_region();
  bool pc_relative_calls_fit_in_code_range =
      !code_region.is_empty() &&
      std::ceil(static_cast<float>(code_region.size() / MB)) <=
          kMaxPCRelativeCodeRangeInMB;

  // Mksnapshot ensures that the code range is small enough to guarantee that
  // PC-relative call/jump instructions can be used for builtin to builtin
  // calls/tail calls. The embedded builtins blob generator also ensures that.
  // However, there are serializer tests, where we force isolate creation at
  // runtime and at this point, Code space isn't restricted to a
  // size s.t. PC-relative calls may be used. So, we fall back to an indirect
  // mode.
  options.use_pc_relative_calls_and_jumps_for_mksnapshot =
      pc_relative_calls_fit_in_code_range;

  options.builtin_call_jump_mode = BuiltinCallJumpMode::kForMksnapshot;
  options.isolate_independent_code = true;
  options.collect_win64_unwind_info = true;

  if (builtin == Builtin::kInterpreterEntryTrampolineForProfiling) {
    // InterpreterEntryTrampolineForProfiling must be generated in a position
    // independent way because it might be necessary to create a copy of the
    // builtin in the code space if the v8_flags.interpreted_frames_native_stack
    // is enabled.
    options.builtin_call_jump_mode = BuiltinCallJumpMode::kIndirect;
  }

  return options;
}

using MacroAssemblerGenerator = void (*)(MacroAssembler*);
using TurboshaftAssemblerGenerator =
    void (*)(compiler::turboshaft::PipelineData*, Isolate*,
             compiler::turboshaft::Graph&, Zone*);
using CodeAssemblerGenerator = compiler::Pipeline::CodeAssemblerGenerator;
using CodeAssemblerInstaller = compiler::Pipeline::CodeAssemblerInstaller;

DirectHandle<Code> BuildPlaceholder(Isolate* isolate, Builtin builtin) {
  HandleScope scope(isolate);
  uint8_t buffer[kBufferSize];
  MacroAssembler masm(isolate, CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, kBufferSize));
  DCHECK(!masm.has_frame());
  {
    FrameScope frame_scope(&masm, StackFrame::NO_FRAME_TYPE);
    // The contents of placeholder don't matter, as long as they don't create
    // embedded constants or external references.
    masm.Move(kJavaScriptCallCodeStartRegister, Smi::zero());
    masm.Call(kJavaScriptCallCodeStartRegister);
  }
  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, CodeKind::BUILTIN)
                          .set_self_reference(masm.CodeObject())
                          .set_builtin(builtin)
                          .Build();
  return scope.CloseAndEscape(code);
}

V8_NOINLINE Tagged<Code> BuildWithMacroAssembler(
    Isolate* isolate, Builtin builtin, MacroAssemblerGenerator generator,
    const char* s_name) {
  HandleScope scope(isolate);
  uint8_t buffer[kBufferSize];

  MacroAssembler masm(isolate, BuiltinAssemblerOptions(isolate, builtin),
                      CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, kBufferSize));
  masm.set_builtin(builtin);
  DCHECK(!masm.has_frame());
  masm.CodeEntry();
  generator(&masm);

  int handler_table_offset = 0;

  // JSEntry builtins are a special case and need to generate a handler table.
  DCHECK_EQ(Builtins::KindOf(Builtin::kJSEntry), Builtins::ASM);
  DCHECK_EQ(Builtins::KindOf(Builtin::kJSConstructEntry), Builtins::ASM);
  DCHECK_EQ(Builtins::KindOf(Builtin::kJSRunMicrotasksEntry), Builtins::ASM);
  if (Builtins::IsJSEntryVariant(builtin)) {
    handler_table_offset = HandlerTable::EmitReturnTableStart(&masm);
    HandlerTable::EmitReturnEntry(
        &masm, 0, isolate->builtins()->js_entry_handler_offset());
#if V8_ENABLE_DRUMBRAKE
  } else if (builtin == Builtin::kWasmInterpreterCWasmEntry) {
    handler_table_offset = HandlerTable::EmitReturnTableStart(&masm);
    HandlerTable::EmitReturnEntry(
        &masm, 0,
        isolate->builtins()->cwasm_interpreter_entry_handler_offset());
#endif  // V8_ENABLE_DRUMBRAKE
  }
#if V8_ENABLE_WEBASSEMBLY &&                                                   \
    (V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_IA32 ||      \
     V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_LOONG64 || \
     V8_TARGET_ARCH_PPC64)
  if (builtin == Builtin::kWasmReturnPromiseOnSuspendAsm) {
    handler_table_offset = HandlerTable::EmitReturnTableStart(&masm);
    HandlerTable::EmitReturnEntry(
        &masm, 0, isolate->builtins()->jspi_prompt_handler_offset());
  }
#endif

  CodeDesc desc;
  masm.GetCode(isolate->main_thread_local_isolate(), &desc,
               MacroAssembler::kNoSafepointTable, handler_table_offset);

  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::BUILTIN)
          .set_self_reference(masm.CodeObject())
          .set_builtin(builtin)
          .Build();
#if defined(V8_OS_WIN64)
  isolate->SetBuiltinUnwindData(builtin, masm.GetUnwindInfo());
#endif  // V8_OS_WIN64
  return *code;
}

Tagged<Code> BuildAdaptor(Isolate* isolate, Builtin builtin,
                          Address builtin_address, const char* name) {
  HandleScope scope(isolate);
  uint8_t buffer[kBufferSize];
  MacroAssembler masm(isolate, BuiltinAssemblerOptions(isolate, builtin),
                      CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, kBufferSize));
  masm.set_builtin(builtin);
  DCHECK(!masm.has_frame());
  int formal_parameter_count = Builtins::GetFormalParameterCount(builtin);
  Builtins::Generate_Adaptor(&masm, formal_parameter_count, builtin_address);
  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::BUILTIN)
          .set_self_reference(masm.CodeObject())
          .set_builtin(builtin)
          .set_parameter_count(formal_parameter_count)
          .Build();
  return *code;
}

// Builder for builtins implemented in Turboshaft with JS linkage.
V8_NOINLINE Tagged<Code> BuildWithTurboshaftAssemblerJS(
    Isolate* isolate, Builtin builtin,
    compiler::turboshaft::TurboshaftAssemblerGenerator generator, int argc,
    const char* name) {
  HandleScope scope(isolate);
  DirectHandle<Code> code =
      compiler::turboshaft::BuildWithTurboshaftAssemblerImpl(
          isolate, builtin, generator,
          [argc](Zone* zone) {
            return compiler::Linkage::GetJSCallDescriptor(
                zone, false, argc, compiler::CallDescriptor::kCanUseRoots);
          },
          name, BuiltinAssemblerOptions(isolate, builtin));
  return *code;
}

void CompileJSLinkageCodeStubBuiltin(Isolate* isolate, Builtin builtin,
                                     CodeAssemblerGenerator generator,
                                     CodeAssemblerInstaller installer, int argc,
                                     const char* name, int finalize_order,
                                     BuiltinCompilationScheduler& scheduler) {
  // TODO(nicohartmann): Remove this once `BuildWithTurboshaftAssemblerJS` has
  // an actual use.
  USE(&BuildWithTurboshaftAssemblerJS);
  std::unique_ptr<TurbofanCompilationJob> job(
      compiler::Pipeline::NewJSLinkageCodeStubBuiltinCompilationJob(
          isolate, builtin, generator, installer,
          BuiltinAssemblerOptions(isolate, builtin), argc, name,
          ProfileDataFromFile::TryRead(name), finalize_order));
  scheduler.CompileCode(isolate, std::move(job));
}

// Builder for builtins implemented in Turboshaft with CallStub linkage.
V8_NOINLINE Tagged<Code> BuildWithTurboshaftAssemblerCS(
    Isolate* isolate, Builtin builtin,
    compiler::turboshaft::TurboshaftAssemblerGenerator generator,
    CallDescriptors::Key interface_descriptor, const char* name) {
  HandleScope scope(isolate);
  DirectHandle<Code> code =
      compiler::turboshaft::BuildWithTurboshaftAssemblerImpl(
          isolate, builtin, generator,
          [interface_descriptor](Zone* zone) {
            CallInterfaceDescriptor descriptor(interface_descriptor);
            DCHECK_LE(0, descriptor.GetRegisterParameterCount());
            return compiler::Linkage::GetStubCallDescriptor(
                zone, descriptor, descriptor.GetStackParameterCount(),
                compiler::CallDescriptor::kNoFlags,
                compiler::Operator::kNoProperties);
          },
          name, BuiltinAssemblerOptions(isolate, builtin));
  return *code;
}

// Builder for builtins implemented in TurboFan with CallStub linkage.
void CompileCSLinkageCodeStubBuiltin(Isolate* isolate, Builtin builtin,
                                     CodeAssemblerGenerator generator,
                                     CodeAssemblerInstaller installer,
                                     CallDescriptors::Key interface_descriptor,
                                     const char* name, int finalize_order,
                                     BuiltinCompilationScheduler& scheduler) {
  // TODO(nicohartmann): Remove this once `BuildWithTurboshaftAssemblerCS` has
  // an actual use.
  USE(&BuildWithTurboshaftAssemblerCS);
  std::unique_ptr<TurbofanCompilationJob> job(
      compiler::Pipeline::NewCSLinkageCodeStubBuiltinCompilationJob(
          isolate, builtin, generator, installer,
          BuiltinAssemblerOptions(isolate, builtin), interface_descriptor, name,
          ProfileDataFromFile::TryRead(name), finalize_order));
  scheduler.CompileCode(isolate, std::move(job));
}

void CompileBytecodeHandler(
    Isolate* isolate, Builtin builtin, interpreter::OperandScale operand_scale,
    interpreter::Bytecode bytecode,
    compiler::Pipeline::CodeAssemblerInstaller installer, int finalize_order,
    BuiltinCompilationScheduler& scheduler) {
  DCHECK(interpreter::Bytecodes::BytecodeHasHandler(bytecode, operand_scale));
  const char* name = Builtins::name(builtin);
  auto generator = [bytecode,
                    operand_scale](compiler::CodeAssemblerState* state) {
    interpreter::GenerateBytecodeHandler(state, bytecode, operand_scale);
  };
  std::unique_ptr<TurbofanCompilationJob> job(
      compiler::Pipeline::NewBytecodeHandlerCompilationJob(
          isolate, builtin, generator, installer,
          BuiltinAssemblerOptions(isolate, builtin), name,
          ProfileDataFromFile::TryRead(name), finalize_order));
  scheduler.CompileCode(isolate, std::move(job));
}

}  // anonymous namespace

// static
void SetupIsolateDelegate::AddBuiltin(Builtins* builtins, Builtin builtin,
                                      Tagged<Code> code) {
  DCHECK_EQ(builtin, code->builtin_id());
  builtins->set_code(builtin, code);
}

// static
void SetupIsolateDelegate::PopulateWithPlaceholders(Isolate* isolate) {
  // Fill the builtins list with placeholders. References to these placeholder
  // builtins are eventually replaced by the actual builtins. This is to
  // support circular references between builtins.
  Builtins* builtins = isolate->builtins();
  HandleScope scope(isolate);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    DirectHandle<Code> placeholder = BuildPlaceholder(isolate, builtin);
    AddBuiltin(builtins, builtin, *placeholder);
  }
}

// static
void SetupIsolateDelegate::ReplacePlaceholders(Isolate* isolate) {
  // Replace references from all builtin code objects to placeholders.
  Builtins* builtins = isolate->builtins();
  DisallowGarbageCollection no_gc;
  static const int kRelocMask =
      RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
      RelocInfo::ModeMask(RelocInfo::FULL_EMBEDDED_OBJECT) |
      RelocInfo::ModeMask(RelocInfo::COMPRESSED_EMBEDDED_OBJECT) |
      RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET);
  PtrComprCageBase cage_base(isolate);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Tagged<Code> code = builtins->code(builtin);
    Tagged<InstructionStream> istream = code->instruction_stream();
    WritableJitAllocation jit_allocation = ThreadIsolation::LookupJitAllocation(
        istream.address(), istream->Size(),
        ThreadIsolation::JitAllocationType::kInstructionStream, true);
    bool flush_icache = false;
    for (WritableRelocIterator it(jit_allocation, istream,
                                  code->constant_pool(), kRelocMask);
         !it.done(); it.next()) {
      WritableRelocInfo* rinfo = it.rinfo();
      if (RelocInfo::IsCodeTargetMode(rinfo->rmode())) {
        Tagged<Code> target_code =
            Code::FromTargetAddress(rinfo->target_address());
        DCHECK_IMPLIES(
            RelocInfo::IsRelativeCodeTarget(rinfo->rmode()),
            Builtins::IsIsolateIndependent(target_code->builtin_id()));
        if (!target_code->is_builtin()) continue;
        Tagged<Code> new_target = builtins->code(target_code->builtin_id());
        rinfo->set_target_address(istream, new_target->instruction_start(),
                                  UPDATE_WRITE_BARRIER, SKIP_ICACHE_FLUSH);
      } else {
        DCHECK(RelocInfo::IsEmbeddedObjectMode(rinfo->rmode()));
        Tagged<Object> object = rinfo->target_object(cage_base);
        if (!IsCode(object, cage_base)) continue;
        Tagged<Code> target = Cast<Code>(object);
        if (!target->is_builtin()) continue;
        Tagged<Code> new_target = builtins->code(target->builtin_id());
        rinfo->set_target_object(istream, new_target, UPDATE_WRITE_BARRIER,
                                 SKIP_ICACHE_FLUSH);
      }
      flush_icache = true;
    }
    if (flush_icache) {
      FlushInstructionCache(code->instruction_start(),
                            code->instruction_size());
    }
  }
}

// static
void SetupIsolateDelegate::SetupBuiltinsInternal(Isolate* isolate) {
  Builtins* builtins = isolate->builtins();
  DCHECK(!builtins->initialized_);

  if (v8_flags.dump_builtins_hashes_to_file) {
    // Create an empty file.
    std::ofstream(v8_flags.dump_builtins_hashes_to_file, std::ios_base::trunc);
  }

  PopulateWithPlaceholders(isolate);

  // Create a scope for the handles in the builtins.
  HandleScope scope(isolate);

  // Generated builtins are temporarily stored in this array to avoid data races
  // on the isolate's builtin table.
  std::array<Handle<Code>, Builtins::kBuiltinCount> generated_builtins;
  auto install_generated_builtin = [&generated_builtins, isolate](
                                       Builtin builtin, Handle<Code> code) {
    DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
    USE(isolate);
    generated_builtins[Builtins::ToInt(builtin)] = code;
  };

  int builtins_built_without_job_count = 0;
  int job_creation_order = 0;
  BuiltinCompilationScheduler scheduler;
  Tagged<Code> code;

#define BUILD_CPP_WITHOUT_JOB(Name, Argc)                    \
  code = BuildAdaptor(isolate, Builtin::k##Name,             \
                      FUNCTION_ADDR(Builtin_##Name), #Name); \
  generated_builtins[Builtins::ToInt(Builtin::k##Name)] =    \
      handle(code, isolate);                                 \
  builtins_built_without_job_count++;

#define BUILD_TSJ_WITHOUT_JOB(Name, Argc, ...)                             \
  code = BuildWithTurboshaftAssemblerJS(                                   \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name, Argc, #Name); \
  AddBuiltin(builtins, Builtin::k##Name, code);                            \
  builtins_built_without_job_count++;

#define BUILD_TFJ_WITH_JOB(Name, Argc, ...)                               \
  CompileJSLinkageCodeStubBuiltin(isolate, Builtin::k##Name,              \
                                  &Builtins::Generate_##Name,             \
                                  install_generated_builtin, Argc, #Name, \
                                  job_creation_order++, scheduler);

#define BUILD_TSC_WITHOUT_JOB(Name, InterfaceDescriptor)          \
  /* Return size is from the provided CallInterfaceDescriptor. */ \
  code = BuildWithTurboshaftAssemblerCS(                          \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name,      \
      CallDescriptors::InterfaceDescriptor, #Name);               \
  generated_builtins[Builtins::ToInt(Builtin::k##Name)] =         \
      handle(code, isolate);                                      \
  builtins_built_without_job_count++;

#define BUILD_TFC_WITH_JOB(Name, InterfaceDescriptor)                         \
  /* Return size is from the provided CallInterfaceDescriptor. */             \
  CompileCSLinkageCodeStubBuiltin(                                            \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name,                  \
      install_generated_builtin, CallDescriptors::InterfaceDescriptor, #Name, \
      job_creation_order++, scheduler);

#define BUILD_TFS_WITH_JOB(Name, ...)                                   \
  /* Return size for generic TF builtins (stub linkage) is always 1. */ \
  CompileCSLinkageCodeStubBuiltin(                                      \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name,            \
      install_generated_builtin, CallDescriptors::Name, #Name,          \
      job_creation_order++, scheduler);

#define BUILD_TFH_WITH_JOB(Name, InterfaceDescriptor)                         \
  /* Return size for IC builtins/handlers is always 1. */                     \
  CompileCSLinkageCodeStubBuiltin(                                            \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name,                  \
      install_generated_builtin, CallDescriptors::InterfaceDescriptor, #Name, \
      job_creation_order++, scheduler);

#define BUILD_BCH_WITH_JOB(Name, OperandScale, Bytecode)                    \
  CompileBytecodeHandler(isolate, Builtin::k##Name, OperandScale, Bytecode, \
                         install_generated_builtin, job_creation_order++,   \
                         scheduler);

#define BUILD_ASM_WITHOUT_JOB(Name, InterfaceDescriptor)            \
  code = BuildWithMacroAssembler(isolate, Builtin::k##Name,         \
                                 Builtins::Generate_##Name, #Name); \
  generated_builtins[Builtins::ToInt(Builtin::k##Name)] =           \
      handle(code, isolate);                                        \
  builtins_built_without_job_count++;

#define NOP(...)

  // Build all builtins without jobs first. When concurrent builtin generation
  // is enabled, allocations needs to be deterministic. Having main-thread
  // generated builtins in the middle of the list breaks determinism.
  BUILTIN_LIST(BUILD_CPP_WITHOUT_JOB, BUILD_TSJ_WITHOUT_JOB, NOP,
               BUILD_TSC_WITHOUT_JOB, NOP, NOP, NOP, NOP,
               BUILD_ASM_WITHOUT_JOB);
  BUILTIN_LIST(NOP, NOP, BUILD_TFJ_WITH_JOB, NOP, BUILD_TFC_WITH_JOB,
               BUILD_TFS_WITH_JOB, BUILD_TFH_WITH_JOB, BUILD_BCH_WITH_JOB, NOP)

#undef BUILD_CPP_WITHOUT_JOB
#undef BUILD_TSJ_WITHOUT_JOB
#undef BUILD_TFJ_WITH_JOB
#undef BUILD_TSC_WITHOUT_JOB
#undef BUILD_TFC_WITH_JOB
#undef BUILD_TFS_WITH_JOB
#undef BUILD_TFH_WITH_JOB
#undef BUILD_BCH_WITH_JOB
#undef BUILD_ASM_WITHOUT_JOB
#undef NOP

  scheduler.AwaitAndFinalizeCurrentBatch(isolate);
  CHECK_EQ(Builtins::kBuiltinCount, builtins_built_without_job_count +
                                        scheduler.builtins_installed_count());

  // Add the generated builtins to the isolate.
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    AddBuiltin(builtins, builtin,
               *generated_builtins[Builtins::ToInt(builtin)]);
  }

  ReplacePlaceholders(isolate);

  builtins->MarkInitialized();
}

}  // namespace internal
}  // namespace v8

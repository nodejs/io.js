// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include "src/builtins/builtins-inl.h"
#include "src/builtins/profile-data-reader.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/reloc-info-inl.h"
#include "src/compiler/code-assembler.h"
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
#define FORWARD_DECLARE(Name) \
  Address Builtin_##Name(int argc, Address* args, Isolate* isolate);
BUILTIN_LIST_C(FORWARD_DECLARE)
#undef FORWARD_DECLARE

namespace {

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
using CodeAssemblerGenerator = void (*)(compiler::CodeAssemblerState*);

Handle<Code> BuildPlaceholder(Isolate* isolate, Builtin builtin) {
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
  }
#if V8_ENABLE_WEBASSEMBLY && (V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64 || \
                              V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_ARM)
  if (builtin == Builtin::kWasmReturnPromiseOnSuspendAsm) {
    handler_table_offset = HandlerTable::EmitReturnTableStart(&masm);
    HandlerTable::EmitReturnEntry(
        &masm, 0, isolate->builtins()->jspi_prompt_handler_offset());
  }
#endif

  CodeDesc desc;
  masm.GetCode(isolate->main_thread_local_isolate(), &desc,
               MacroAssembler::kNoSafepointTable, handler_table_offset);

  Handle<Code> code = Factory::CodeBuilder(isolate, desc, CodeKind::BUILTIN)
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
  Builtins::Generate_Adaptor(&masm, builtin_address);
  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, CodeKind::BUILTIN)
                          .set_self_reference(masm.CodeObject())
                          .set_builtin(builtin)
                          .Build();
  return *code;
}

// Builder for builtins implemented in TurboFan with JS linkage.
V8_NOINLINE Tagged<Code> BuildWithCodeStubAssemblerJS(
    Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
    int argc, const char* name) {
  HandleScope scope(isolate);

  Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
  compiler::CodeAssemblerState state(isolate, &zone, argc, CodeKind::BUILTIN,
                                     name, builtin);
  generator(&state);
  Handle<Code> code = compiler::CodeAssembler::GenerateCode(
      &state, BuiltinAssemblerOptions(isolate, builtin),
      ProfileDataFromFile::TryRead(name));
  return *code;
}

// Builder for builtins implemented in TurboFan with CallStub linkage.
V8_NOINLINE Tagged<Code> BuildWithCodeStubAssemblerCS(
    Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
    CallDescriptors::Key interface_descriptor, const char* name) {
  HandleScope scope(isolate);
  Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
  // The interface descriptor with given key must be initialized at this point
  // and this construction just queries the details from the descriptors table.
  CallInterfaceDescriptor descriptor(interface_descriptor);
  // Ensure descriptor is already initialized.
  DCHECK_LE(0, descriptor.GetRegisterParameterCount());
  compiler::CodeAssemblerState state(isolate, &zone, descriptor,
                                     CodeKind::BUILTIN, name, builtin);
  generator(&state);
  Handle<Code> code = compiler::CodeAssembler::GenerateCode(
      &state, BuiltinAssemblerOptions(isolate, builtin),
      ProfileDataFromFile::TryRead(name));
  return *code;
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
    Handle<Code> placeholder = BuildPlaceholder(isolate, builtin);
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
        ThreadIsolation::JitAllocationType::kInstructionStream);
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
        Tagged<Code> target = Code::cast(object);
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

namespace {

V8_NOINLINE Tagged<Code> GenerateBytecodeHandler(
    Isolate* isolate, Builtin builtin, interpreter::OperandScale operand_scale,
    interpreter::Bytecode bytecode) {
  DCHECK(interpreter::Bytecodes::BytecodeHasHandler(bytecode, operand_scale));
  Handle<Code> code = interpreter::GenerateBytecodeHandler(
      isolate, Builtins::name(builtin), bytecode, operand_scale, builtin,
      BuiltinAssemblerOptions(isolate, builtin));
  return *code;
}

}  // namespace

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

  int index = 0;
  Tagged<Code> code;
#define BUILD_CPP(Name)                                      \
  code = BuildAdaptor(isolate, Builtin::k##Name,             \
                      FUNCTION_ADDR(Builtin_##Name), #Name); \
  AddBuiltin(builtins, Builtin::k##Name, code);              \
  index++;

#define BUILD_TFJ(Name, Argc, ...)                                         \
  code = BuildWithCodeStubAssemblerJS(                                     \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name, Argc, #Name); \
  AddBuiltin(builtins, Builtin::k##Name, code);                            \
  index++;

#define BUILD_TFC(Name, InterfaceDescriptor)                      \
  /* Return size is from the provided CallInterfaceDescriptor. */ \
  code = BuildWithCodeStubAssemblerCS(                            \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name,      \
      CallDescriptors::InterfaceDescriptor, #Name);               \
  AddBuiltin(builtins, Builtin::k##Name, code);                   \
  index++;

#define BUILD_TFS(Name, ...)                                            \
  /* Return size for generic TF builtins (stub linkage) is always 1. */ \
  code = BuildWithCodeStubAssemblerCS(isolate, Builtin::k##Name,        \
                                      &Builtins::Generate_##Name,       \
                                      CallDescriptors::Name, #Name);    \
  AddBuiltin(builtins, Builtin::k##Name, code);                         \
  index++;

#define BUILD_TFH(Name, InterfaceDescriptor)                 \
  /* Return size for IC builtins/handlers is always 1. */    \
  code = BuildWithCodeStubAssemblerCS(                       \
      isolate, Builtin::k##Name, &Builtins::Generate_##Name, \
      CallDescriptors::InterfaceDescriptor, #Name);          \
  AddBuiltin(builtins, Builtin::k##Name, code);              \
  index++;

#define BUILD_BCH(Name, OperandScale, Bytecode)                           \
  code = GenerateBytecodeHandler(isolate, Builtin::k##Name, OperandScale, \
                                 Bytecode);                               \
  AddBuiltin(builtins, Builtin::k##Name, code);                           \
  index++;

#define BUILD_ASM(Name, InterfaceDescriptor)                        \
  code = BuildWithMacroAssembler(isolate, Builtin::k##Name,         \
                                 Builtins::Generate_##Name, #Name); \
  AddBuiltin(builtins, Builtin::k##Name, code);                     \
  index++;

  BUILTIN_LIST(BUILD_CPP, BUILD_TFJ, BUILD_TFC, BUILD_TFS, BUILD_TFH, BUILD_BCH,
               BUILD_ASM);

#undef BUILD_CPP
#undef BUILD_TFJ
#undef BUILD_TFC
#undef BUILD_TFS
#undef BUILD_TFH
#undef BUILD_BCH
#undef BUILD_ASM
  CHECK_EQ(Builtins::kBuiltinCount, index);

  ReplacePlaceholders(isolate);

  builtins->MarkInitialized();
}

}  // namespace internal
}  // namespace v8

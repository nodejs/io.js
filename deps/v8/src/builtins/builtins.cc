// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"

#include "src/api/api-inl.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/builtins/data-view-ops.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/code-tracer.h"
#include "src/execution/isolate.h"
#include "src/interpreter/bytecodes.h"
#include "src/logging/code-events.h"  // For CodeCreateEvent.
#include "src/logging/log.h"          // For V8FileLogger.
#include "src/objects/fixed-array.h"
#include "src/objects/objects-inl.h"
#include "src/objects/visitors.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

// Forward declarations for C++ builtins.
#define FORWARD_DECLARE(Name) \
  Address Builtin_##Name(int argc, Address* args, Isolate* isolate);
BUILTIN_LIST_C(FORWARD_DECLARE)
#undef FORWARD_DECLARE

namespace {

// TODO(jgruber): Pack in CallDescriptors::Key.
struct BuiltinMetadata {
  const char* name;
  Builtins::Kind kind;

  struct BytecodeAndScale {
    interpreter::Bytecode bytecode : 8;
    interpreter::OperandScale scale : 8;
  };

  static_assert(sizeof(interpreter::Bytecode) == 1);
  static_assert(sizeof(interpreter::OperandScale) == 1);
  static_assert(sizeof(BytecodeAndScale) <= sizeof(Address));

  // The `data` field has kind-specific contents.
  union KindSpecificData {
    // TODO(jgruber): Union constructors are needed since C++11 does not support
    // designated initializers (e.g.: {.parameter_count = count}). Update once
    // we're at C++20 :)
    // The constructors are marked constexpr to avoid the need for a static
    // initializer for builtins.cc (see check-static-initializers.sh).
    constexpr KindSpecificData() : cpp_entry(kNullAddress) {}
    constexpr KindSpecificData(Address cpp_entry) : cpp_entry(cpp_entry) {}
    constexpr KindSpecificData(int parameter_count,
                               int /* To disambiguate from above */)
        : parameter_count(static_cast<int16_t>(parameter_count)) {}
    constexpr KindSpecificData(interpreter::Bytecode bytecode,
                               interpreter::OperandScale scale)
        : bytecode_and_scale{bytecode, scale} {}
    Address cpp_entry;                    // For CPP builtins.
    int16_t parameter_count;              // For TFJ builtins.
    BytecodeAndScale bytecode_and_scale;  // For BCH builtins.
  } data;
};

#define DECL_CPP(Name, ...) \
  {#Name, Builtins::CPP, {FUNCTION_ADDR(Builtin_##Name)}},
#define DECL_TFJ(Name, Count, ...) {#Name, Builtins::TFJ, {Count, 0}},
#define DECL_TFC(Name, ...) {#Name, Builtins::TFC, {}},
#define DECL_TFS(Name, ...) {#Name, Builtins::TFS, {}},
#define DECL_TFH(Name, ...) {#Name, Builtins::TFH, {}},
#define DECL_BCH(Name, OperandScale, Bytecode) \
  {#Name, Builtins::BCH, {Bytecode, OperandScale}},
#define DECL_ASM(Name, ...) {#Name, Builtins::ASM, {}},
const BuiltinMetadata builtin_metadata[] = {BUILTIN_LIST(
    DECL_CPP, DECL_TFJ, DECL_TFC, DECL_TFS, DECL_TFH, DECL_BCH, DECL_ASM)};
#undef DECL_CPP
#undef DECL_TFJ
#undef DECL_TFC
#undef DECL_TFS
#undef DECL_TFH
#undef DECL_BCH
#undef DECL_ASM

}  // namespace

BytecodeOffset Builtins::GetContinuationBytecodeOffset(Builtin builtin) {
  DCHECK(Builtins::KindOf(builtin) == TFJ || Builtins::KindOf(builtin) == TFC ||
         Builtins::KindOf(builtin) == TFS);
  return BytecodeOffset(BytecodeOffset::kFirstBuiltinContinuationId +
                        ToInt(builtin));
}

Builtin Builtins::GetBuiltinFromBytecodeOffset(BytecodeOffset id) {
  Builtin builtin = Builtins::FromInt(
      id.ToInt() - BytecodeOffset::kFirstBuiltinContinuationId);
  DCHECK(Builtins::KindOf(builtin) == TFJ || Builtins::KindOf(builtin) == TFC ||
         Builtins::KindOf(builtin) == TFS);
  return builtin;
}

void Builtins::TearDown() { initialized_ = false; }

const char* Builtins::Lookup(Address pc) {
  // Off-heap pc's can be looked up through binary search.
  Builtin builtin = OffHeapInstructionStream::TryLookupCode(isolate_, pc);
  if (Builtins::IsBuiltinId(builtin)) return name(builtin);

  // May be called during initialization (disassembler).
  if (!initialized_) return nullptr;
  for (Builtin builtin_ix = Builtins::kFirst; builtin_ix <= Builtins::kLast;
       ++builtin_ix) {
    if (code(builtin_ix)->contains(isolate_, pc)) {
      return name(builtin_ix);
    }
  }
  return nullptr;
}

FullObjectSlot Builtins::builtin_slot(Builtin builtin) {
  Address* location = &isolate_->builtin_table()[Builtins::ToInt(builtin)];
  return FullObjectSlot(location);
}

FullObjectSlot Builtins::builtin_tier0_slot(Builtin builtin) {
  DCHECK(IsTier0(builtin));
  Address* location =
      &isolate_->builtin_tier0_table()[Builtins::ToInt(builtin)];
  return FullObjectSlot(location);
}

void Builtins::set_code(Builtin builtin, Tagged<Code> code) {
  DCHECK_EQ(builtin, code->builtin_id());
  DCHECK(Internals::HasHeapObjectTag(code.ptr()));
  // The given builtin may be uninitialized thus we cannot check its type here.
  isolate_->builtin_table()[Builtins::ToInt(builtin)] = code.ptr();
}

Tagged<Code> Builtins::code(Builtin builtin) {
  Address ptr = isolate_->builtin_table()[Builtins::ToInt(builtin)];
  return Code::cast(Tagged<Object>(ptr));
}

Handle<Code> Builtins::code_handle(Builtin builtin) {
  Address* location = &isolate_->builtin_table()[Builtins::ToInt(builtin)];
  return Handle<Code>(location);
}

// static
int Builtins::GetStackParameterCount(Builtin builtin) {
  DCHECK(Builtins::KindOf(builtin) == TFJ);
  return builtin_metadata[ToInt(builtin)].data.parameter_count;
}

// static
CallInterfaceDescriptor Builtins::CallInterfaceDescriptorFor(Builtin builtin) {
  CallDescriptors::Key key;
  switch (builtin) {
// This macro is deliberately crafted so as to emit very little code,
// in order to keep binary size of this function under control.
#define CASE_OTHER(Name, ...)                          \
  case Builtin::k##Name: {                             \
    key = Builtin_##Name##_InterfaceDescriptor::key(); \
    break;                                             \
  }
    BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, CASE_OTHER, CASE_OTHER,
                 CASE_OTHER, IGNORE_BUILTIN, CASE_OTHER)
#undef CASE_OTHER
    default:
      Builtins::Kind kind = Builtins::KindOf(builtin);
      DCHECK_NE(BCH, kind);
      if (kind == TFJ || kind == CPP) {
        return JSTrampolineDescriptor{};
      }
      UNREACHABLE();
  }
  return CallInterfaceDescriptor{key};
}

// static
Callable Builtins::CallableFor(Isolate* isolate, Builtin builtin) {
  Handle<Code> code = isolate->builtins()->code_handle(builtin);
  return Callable{code, CallInterfaceDescriptorFor(builtin)};
}

// static
bool Builtins::HasJSLinkage(Builtin builtin) {
  DCHECK_NE(BCH, Builtins::KindOf(builtin));
  return CallInterfaceDescriptorFor(builtin) == JSTrampolineDescriptor{};
}

// static
const char* Builtins::name(Builtin builtin) {
  int index = ToInt(builtin);
  DCHECK(IsBuiltinId(index));
  return builtin_metadata[index].name;
}

// static
const char* Builtins::NameForStackTrace(Isolate* isolate, Builtin builtin) {
#if V8_ENABLE_WEBASSEMBLY
  // Most builtins are never shown in stack traces. Those that are exposed
  // to JavaScript get their name from the object referring to them. Here
  // we only support a few internal builtins that have special reasons for
  // being shown on stack traces:
  // - builtins that are allowlisted in {StubFrame::Summarize}.
  // - builtins that throw the same error as one of those above, but would
  //   lose information and e.g. print "indexOf" instead of "String.indexOf".
  switch (builtin) {
    case Builtin::kDataViewPrototypeGetBigInt64:
      return "DataView.prototype.getBigInt64";
    case Builtin::kDataViewPrototypeGetBigUint64:
      return "DataView.prototype.getBigUint64";
    case Builtin::kDataViewPrototypeGetFloat16:
      return "DataView.prototype.getFloat16";
    case Builtin::kDataViewPrototypeGetFloat32:
      return "DataView.prototype.getFloat32";
    case Builtin::kDataViewPrototypeGetFloat64:
      return "DataView.prototype.getFloat64";
    case Builtin::kDataViewPrototypeGetInt8:
      return "DataView.prototype.getInt8";
    case Builtin::kDataViewPrototypeGetInt16:
      return "DataView.prototype.getInt16";
    case Builtin::kDataViewPrototypeGetInt32:
      return "DataView.prototype.getInt32";
    case Builtin::kDataViewPrototypeGetUint8:
      return "DataView.prototype.getUint8";
    case Builtin::kDataViewPrototypeGetUint16:
      return "DataView.prototype.getUint16";
    case Builtin::kDataViewPrototypeGetUint32:
      return "DataView.prototype.getUint32";
    case Builtin::kDataViewPrototypeSetBigInt64:
      return "DataView.prototype.setBigInt64";
    case Builtin::kDataViewPrototypeSetBigUint64:
      return "DataView.prototype.setBigUint64";
    case Builtin::kDataViewPrototypeSetFloat16:
      return "DataView.prototype.setFloat16";
    case Builtin::kDataViewPrototypeSetFloat32:
      return "DataView.prototype.setFloat32";
    case Builtin::kDataViewPrototypeSetFloat64:
      return "DataView.prototype.setFloat64";
    case Builtin::kDataViewPrototypeSetInt8:
      return "DataView.prototype.setInt8";
    case Builtin::kDataViewPrototypeSetInt16:
      return "DataView.prototype.setInt16";
    case Builtin::kDataViewPrototypeSetInt32:
      return "DataView.prototype.setInt32";
    case Builtin::kDataViewPrototypeSetUint8:
      return "DataView.prototype.setUint8";
    case Builtin::kDataViewPrototypeSetUint16:
      return "DataView.prototype.setUint16";
    case Builtin::kDataViewPrototypeSetUint32:
      return "DataView.prototype.setUint32";
    case Builtin::kDataViewPrototypeGetByteLength:
      return "get DataView.prototype.byteLength";
    case Builtin::kThrowDataViewDetachedError:
    case Builtin::kThrowDataViewOutOfBounds:
    case Builtin::kThrowDataViewTypeError: {
      DataViewOp op = static_cast<DataViewOp>(isolate->error_message_param());
      return ToString(op);
    }
    case Builtin::kStringPrototypeToLocaleLowerCase:
      return "String.toLocaleLowerCase";
    case Builtin::kStringPrototypeIndexOf:
    case Builtin::kThrowIndexOfCalledOnNull:
      return "String.indexOf";
#if V8_INTL_SUPPORT
    case Builtin::kStringPrototypeToLowerCaseIntl:
#endif
    case Builtin::kThrowToLowerCaseCalledOnNull:
      return "String.toLowerCase";
    case Builtin::kWasmIntToString:
      return "Number.toString";
    default:
      // Callers getting this might well crash, which might be desirable
      // because it's similar to {UNREACHABLE()}, but contrary to that a
      // careful caller can also check the value and use it as an "is a
      // name available for this builtin?" check.
      return nullptr;
  }
#else
  return nullptr;
#endif  // V8_ENABLE_WEBASSEMBLY
}

void Builtins::PrintBuiltinCode() {
  DCHECK(v8_flags.print_builtin_code);
#ifdef ENABLE_DISASSEMBLER
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    const char* builtin_name = name(builtin);
    if (PassesFilter(base::CStrVector(builtin_name),
                     base::CStrVector(v8_flags.print_builtin_code_filter))) {
      CodeTracer::Scope trace_scope(isolate_->GetCodeTracer());
      OFStream os(trace_scope.file());
      Tagged<Code> builtin_code = code(builtin);
      builtin_code->Disassemble(builtin_name, os, isolate_);
      os << "\n";
    }
  }
#endif
}

void Builtins::PrintBuiltinSize() {
  DCHECK(v8_flags.print_builtin_size);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    const char* builtin_name = name(builtin);
    const char* kind = KindNameOf(builtin);
    Tagged<Code> code = Builtins::code(builtin);
    PrintF(stdout, "%s Builtin, %s, %d\n", kind, builtin_name,
           code->instruction_size());
  }
}

// static
Address Builtins::CppEntryOf(Builtin builtin) {
  DCHECK(Builtins::IsCpp(builtin));
  return builtin_metadata[ToInt(builtin)].data.cpp_entry;
}

// static
bool Builtins::IsBuiltin(const Tagged<Code> code) {
  return Builtins::IsBuiltinId(code->builtin_id());
}

bool Builtins::IsBuiltinHandle(Handle<HeapObject> maybe_code,
                               Builtin* builtin) const {
  Address* handle_location = maybe_code.location();
  Address* builtins_table = isolate_->builtin_table();
  if (handle_location < builtins_table) return false;
  Address* builtins_table_end = &builtins_table[Builtins::kBuiltinCount];
  if (handle_location >= builtins_table_end) return false;
  *builtin = FromInt(static_cast<int>(handle_location - builtins_table));
  return true;
}

// static
bool Builtins::IsIsolateIndependentBuiltin(Tagged<Code> code) {
  Builtin builtin = code->builtin_id();
  return Builtins::IsBuiltinId(builtin) &&
         Builtins::IsIsolateIndependent(builtin);
}

// static
void Builtins::InitializeIsolateDataTables(Isolate* isolate) {
  EmbeddedData embedded_data = EmbeddedData::FromBlob(isolate);
  IsolateData* isolate_data = isolate->isolate_data();

  // The entry table.
  for (Builtin i = Builtins::kFirst; i <= Builtins::kLast; ++i) {
    DCHECK(Builtins::IsBuiltinId(isolate->builtins()->code(i)->builtin_id()));
    DCHECK(!isolate->builtins()->code(i)->has_instruction_stream());
    isolate_data->builtin_entry_table()[ToInt(i)] =
        embedded_data.InstructionStartOf(i);
  }

  // T0 tables.
  for (Builtin i = Builtins::kFirst; i <= Builtins::kLastTier0; ++i) {
    const int ii = ToInt(i);
    isolate_data->builtin_tier0_entry_table()[ii] =
        isolate_data->builtin_entry_table()[ii];
    isolate_data->builtin_tier0_table()[ii] = isolate_data->builtin_table()[ii];
  }
}

// static
void Builtins::EmitCodeCreateEvents(Isolate* isolate) {
  if (!isolate->IsLoggingCodeCreation()) return;

  Address* builtins = isolate->builtin_table();
  int i = 0;
  HandleScope scope(isolate);
  for (; i < ToInt(Builtin::kFirstBytecodeHandler); i++) {
    Handle<Code> builtin_code(&builtins[i]);
    Handle<AbstractCode> code = Handle<AbstractCode>::cast(builtin_code);
    PROFILE(isolate, CodeCreateEvent(LogEventListener::CodeTag::kBuiltin, code,
                                     Builtins::name(FromInt(i))));
  }

  static_assert(kLastBytecodeHandlerPlusOne == kBuiltinCount);
  for (; i < kBuiltinCount; i++) {
    Handle<Code> builtin_code(&builtins[i]);
    Handle<AbstractCode> code = Handle<AbstractCode>::cast(builtin_code);
    interpreter::Bytecode bytecode =
        builtin_metadata[i].data.bytecode_and_scale.bytecode;
    interpreter::OperandScale scale =
        builtin_metadata[i].data.bytecode_and_scale.scale;
    PROFILE(isolate,
            CodeCreateEvent(
                LogEventListener::CodeTag::kBytecodeHandler, code,
                interpreter::Bytecodes::ToString(bytecode, scale).c_str()));
  }
}

// static
Handle<Code> Builtins::CreateInterpreterEntryTrampolineForProfiling(
    Isolate* isolate) {
  DCHECK_NOT_NULL(isolate->embedded_blob_code());
  DCHECK_NE(0, isolate->embedded_blob_code_size());

  Tagged<Code> code = isolate->builtins()->code(
      Builtin::kInterpreterEntryTrampolineForProfiling);

  CodeDesc desc;
  desc.buffer = reinterpret_cast<uint8_t*>(code->instruction_start());

  int instruction_size = code->instruction_size();
  desc.buffer_size = instruction_size;
  desc.instr_size = instruction_size;

  // Ensure the code doesn't require creation of metadata, otherwise respective
  // fields of CodeDesc should be initialized.
  DCHECK_EQ(code->safepoint_table_size(), 0);
  DCHECK_EQ(code->handler_table_size(), 0);
  DCHECK_EQ(code->constant_pool_size(), 0);
  // TODO(v8:11036): The following DCHECK currently fails if the mksnapshot is
  // run with enabled code comments, i.e. --interpreted_frames_native_stack is
  // incompatible with --code-comments at mksnapshot-time. If ever needed,
  // implement support.
  DCHECK_EQ(code->code_comments_size(), 0);
  DCHECK_EQ(code->unwinding_info_size(), 0);

  desc.safepoint_table_offset = instruction_size;
  desc.handler_table_offset = instruction_size;
  desc.constant_pool_offset = instruction_size;
  desc.code_comments_offset = instruction_size;

  CodeDesc::Verify(&desc);

  return Factory::CodeBuilder(isolate, desc, CodeKind::BUILTIN)
      // Mimic the InterpreterEntryTrampoline.
      .set_builtin(Builtin::kInterpreterEntryTrampoline)
      .Build();
}

Builtins::Kind Builtins::KindOf(Builtin builtin) {
  DCHECK(IsBuiltinId(builtin));
  return builtin_metadata[ToInt(builtin)].kind;
}

// static
const char* Builtins::KindNameOf(Builtin builtin) {
  Kind kind = Builtins::KindOf(builtin);
  // clang-format off
  switch (kind) {
    case CPP: return "CPP";
    case TFJ: return "TFJ";
    case TFC: return "TFC";
    case TFS: return "TFS";
    case TFH: return "TFH";
    case BCH: return "BCH";
    case ASM: return "ASM";
  }
  // clang-format on
  UNREACHABLE();
}

// static
bool Builtins::IsCpp(Builtin builtin) {
  return Builtins::KindOf(builtin) == CPP;
}

// static
CodeEntrypointTag Builtins::EntrypointTagFor(Builtin builtin) {
  if (builtin == Builtin::kNoBuiltinId) {
    // Special case needed for example for tests.
    return kDefaultCodeEntrypointTag;
  }

  Kind kind = Builtins::KindOf(builtin);
  switch (kind) {
    case CPP:
    case TFJ:
      return kJSEntrypointTag;
    case BCH:
      return kBytecodeHandlerEntrypointTag;
    case TFC:
    case TFS:
    case TFH:
    case ASM:
      return CallInterfaceDescriptorFor(builtin).tag();
  }
  UNREACHABLE();
}

// static
bool Builtins::AllowDynamicFunction(Isolate* isolate, Handle<JSFunction> target,
                                    Handle<JSObject> target_global_proxy) {
  if (v8_flags.allow_unsafe_function_constructor) return true;
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  Handle<NativeContext> responsible_context = impl->LastEnteredContext();
  // TODO(verwaest): Remove this.
  if (responsible_context.is_null()) {
    return true;
  }
  if (*responsible_context == target->context()) return true;
  return isolate->MayAccess(responsible_context, target_global_proxy);
}

Builtin ExampleBuiltinForTorqueFunctionPointerType(
    size_t function_pointer_type_id) {
  switch (function_pointer_type_id) {
#define FUNCTION_POINTER_ID_CASE(id, name) \
  case id:                                 \
    return Builtin::k##name;
    TORQUE_FUNCTION_POINTER_TYPE_TO_BUILTIN_MAP(FUNCTION_POINTER_ID_CASE)
#undef FUNCTION_POINTER_ID_CASE
    default:
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8

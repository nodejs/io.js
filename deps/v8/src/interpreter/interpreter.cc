// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter.h"

#include <fstream>
#include <memory>

#include "builtins-generated/bytecodes-builtins-list.h"
#include "src/ast/prettyprinter.h"
#include "src/ast/scopes.h"
#include "src/codegen/compiler.h"
#include "src/codegen/unoptimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/execution/local-isolate.h"
#include "src/heap/parked-scope.h"
#include "src/init/setup-isolate.h"
#include "src/interpreter/bytecode-generator.h"
#include "src/interpreter/bytecodes.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/parsing/parse-info.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {
namespace interpreter {

class InterpreterCompilationJob final : public UnoptimizedCompilationJob {
 public:
  InterpreterCompilationJob(ParseInfo* parse_info, FunctionLiteral* literal,
                            Handle<Script> script,
                            AccountingAllocator* allocator,
                            std::vector<FunctionLiteral*>* eager_inner_literals,
                            LocalIsolate* local_isolate);
  InterpreterCompilationJob(const InterpreterCompilationJob&) = delete;
  InterpreterCompilationJob& operator=(const InterpreterCompilationJob&) =
      delete;

 protected:
  Status ExecuteJobImpl() final;
  Status FinalizeJobImpl(Handle<SharedFunctionInfo> shared_info,
                         Isolate* isolate) final;
  Status FinalizeJobImpl(Handle<SharedFunctionInfo> shared_info,
                         LocalIsolate* isolate) final;

 private:
  BytecodeGenerator* generator() { return &generator_; }
  template <typename IsolateT>
  void CheckAndPrintBytecodeMismatch(IsolateT* isolate, Handle<Script> script,
                                     Handle<BytecodeArray> bytecode);

  template <typename IsolateT>
  Status DoFinalizeJobImpl(Handle<SharedFunctionInfo> shared_info,
                           IsolateT* isolate);

  Zone zone_;
  UnoptimizedCompilationInfo compilation_info_;
  LocalIsolate* local_isolate_;
  BytecodeGenerator generator_;
};

Interpreter::Interpreter(Isolate* isolate)
    : isolate_(isolate),
      interpreter_entry_trampoline_instruction_start_(kNullAddress) {
  memset(dispatch_table_, 0, sizeof(dispatch_table_));

  if (V8_IGNITION_DISPATCH_COUNTING_BOOL) {
    InitDispatchCounters();
  }
}

void Interpreter::InitDispatchCounters() {
  static const int kBytecodeCount = static_cast<int>(Bytecode::kLast) + 1;
  bytecode_dispatch_counters_table_.reset(
      new uintptr_t[kBytecodeCount * kBytecodeCount]);
  memset(bytecode_dispatch_counters_table_.get(), 0,
         sizeof(uintptr_t) * kBytecodeCount * kBytecodeCount);
}

namespace {

Builtin BuiltinIndexFromBytecode(Bytecode bytecode,
                                 OperandScale operand_scale) {
  int index = static_cast<int>(bytecode);
  if (operand_scale == OperandScale::kSingle) {
    if (Bytecodes::IsShortStar(bytecode)) {
      index = static_cast<int>(Bytecode::kFirstShortStar);
    } else if (bytecode > Bytecode::kLastShortStar) {
      // Adjust the index due to repeated handlers.
      index -= Bytecodes::kShortStarCount - 1;
    }
  } else {
    // The table contains uint8_t offsets starting at 0 with
    // kIllegalBytecodeHandlerEncoding for illegal bytecode/scale combinations.
    uint8_t offset = kWideBytecodeToBuiltinsMapping[index];
    if (offset == kIllegalBytecodeHandlerEncoding) {
      return Builtin::kIllegalHandler;
    } else {
      index = kNumberOfBytecodeHandlers + offset;
      if (operand_scale == OperandScale::kQuadruple) {
        index += kNumberOfWideBytecodeHandlers;
      }
    }
  }
  return Builtins::FromInt(static_cast<int>(Builtin::kFirstBytecodeHandler) +
                           index);
}

}  // namespace

Tagged<Code> Interpreter::GetBytecodeHandler(Bytecode bytecode,
                                             OperandScale operand_scale) {
  Builtin builtin = BuiltinIndexFromBytecode(bytecode, operand_scale);
  return isolate_->builtins()->code(builtin);
}

void Interpreter::SetBytecodeHandler(Bytecode bytecode,
                                     OperandScale operand_scale,
                                     Tagged<Code> handler) {
  DCHECK(!handler->has_instruction_stream());
  DCHECK(handler->kind() == CodeKind::BYTECODE_HANDLER);
  size_t index = GetDispatchTableIndex(bytecode, operand_scale);
  dispatch_table_[index] = handler->instruction_start();
}

// static
size_t Interpreter::GetDispatchTableIndex(Bytecode bytecode,
                                          OperandScale operand_scale) {
  static const size_t kEntriesPerOperandScale = 1u << kBitsPerByte;
  size_t index = static_cast<size_t>(bytecode);
  return index + BytecodeOperands::OperandScaleAsIndex(operand_scale) *
                     kEntriesPerOperandScale;
}

namespace {

void MaybePrintAst(ParseInfo* parse_info,
                   UnoptimizedCompilationInfo* compilation_info) {
  if (!v8_flags.print_ast) return;

  StdoutStream os;
  std::unique_ptr<char[]> name = compilation_info->literal()->GetDebugName();
  os << "[generating bytecode for function: " << name.get() << "]" << std::endl;
#ifdef DEBUG
  os << "--- AST ---" << std::endl
     << AstPrinter(parse_info->stack_limit())
            .PrintProgram(compilation_info->literal())
     << std::endl;
#endif  // DEBUG
}

bool ShouldPrintBytecode(Handle<SharedFunctionInfo> shared) {
  if (!v8_flags.print_bytecode) return false;

  // Checks whether function passed the filter.
  if (shared->is_toplevel()) {
    base::Vector<const char> filter =
        base::CStrVector(v8_flags.print_bytecode_filter);
    return filter.empty() || (filter.length() == 1 && filter[0] == '*');
  } else {
    return shared->PassesFilter(v8_flags.print_bytecode_filter);
  }
}

}  // namespace

InterpreterCompilationJob::InterpreterCompilationJob(
    ParseInfo* parse_info, FunctionLiteral* literal, Handle<Script> script,
    AccountingAllocator* allocator,
    std::vector<FunctionLiteral*>* eager_inner_literals,
    LocalIsolate* local_isolate)
    : UnoptimizedCompilationJob(parse_info->stack_limit(), parse_info,
                                &compilation_info_),
      zone_(allocator, ZONE_NAME),
      compilation_info_(&zone_, parse_info, literal),
      local_isolate_(local_isolate),
      generator_(local_isolate, &zone_, &compilation_info_,
                 parse_info->ast_string_constants(), eager_inner_literals,
                 script) {}

InterpreterCompilationJob::Status InterpreterCompilationJob::ExecuteJobImpl() {
  RCS_SCOPE(parse_info()->runtime_call_stats(),
            RuntimeCallCounterId::kCompileIgnition,
            RuntimeCallStats::kThreadSpecific);
  // TODO(lpy): add support for background compilation RCS trace.
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.CompileIgnition");

  // Print AST if flag is enabled. Note, if compiling on a background thread
  // then ASTs from different functions may be intersperse when printed.
  {
    DisallowGarbageCollection no_heap_access;
    MaybePrintAst(parse_info(), compilation_info());
  }

  ParkedScopeIfOnBackground parked_scope(local_isolate_);

  generator()->GenerateBytecode(stack_limit());

  if (generator()->HasStackOverflow()) {
    return FAILED;
  }
  return SUCCEEDED;
}

#ifdef DEBUG
template <typename IsolateT>
void InterpreterCompilationJob::CheckAndPrintBytecodeMismatch(
    IsolateT* isolate, Handle<Script> script, Handle<BytecodeArray> bytecode) {
  int first_mismatch = generator()->CheckBytecodeMatches(*bytecode);
  if (first_mismatch >= 0) {
    parse_info()->ast_value_factory()->Internalize(isolate);
    DeclarationScope::AllocateScopeInfos(parse_info(), isolate);

    Handle<BytecodeArray> new_bytecode =
        generator()->FinalizeBytecode(isolate, script);

    std::cerr << "Bytecode mismatch";
#ifdef OBJECT_PRINT
    std::cerr << " found for function: ";
    MaybeHandle<String> maybe_name = parse_info()->literal()->GetName(isolate);
    Handle<String> name;
    if (maybe_name.ToHandle(&name) && name->length() != 0) {
      name->PrintUC16(std::cerr);
    } else {
      std::cerr << "anonymous";
    }
    Tagged<Object> script_name = script->GetNameOrSourceURL();
    if (IsString(script_name)) {
      std::cerr << " ";
      String::cast(script_name)->PrintUC16(std::cerr);
      std::cerr << ":" << parse_info()->literal()->start_position();
    }
#endif
    std::cerr << "\nOriginal bytecode:\n";
    bytecode->Disassemble(std::cerr);
    std::cerr << "\nNew bytecode:\n";
    new_bytecode->Disassemble(std::cerr);
    FATAL("Bytecode mismatch at offset %d\n", first_mismatch);
  }
}
#endif

InterpreterCompilationJob::Status InterpreterCompilationJob::FinalizeJobImpl(
    Handle<SharedFunctionInfo> shared_info, Isolate* isolate) {
  RCS_SCOPE(parse_info()->runtime_call_stats(),
            RuntimeCallCounterId::kCompileIgnitionFinalization);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.CompileIgnitionFinalization");
  return DoFinalizeJobImpl(shared_info, isolate);
}

InterpreterCompilationJob::Status InterpreterCompilationJob::FinalizeJobImpl(
    Handle<SharedFunctionInfo> shared_info, LocalIsolate* isolate) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileIgnitionFinalization,
            RuntimeCallStats::kThreadSpecific);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.CompileIgnitionFinalization");
  return DoFinalizeJobImpl(shared_info, isolate);
}

template <typename IsolateT>
InterpreterCompilationJob::Status InterpreterCompilationJob::DoFinalizeJobImpl(
    Handle<SharedFunctionInfo> shared_info, IsolateT* isolate) {
  Handle<BytecodeArray> bytecodes = compilation_info_.bytecode_array();
  if (bytecodes.is_null()) {
    bytecodes = generator()->FinalizeBytecode(
        isolate, handle(Script::cast(shared_info->script()), isolate));
    if (generator()->HasStackOverflow()) {
      return FAILED;
    }
    compilation_info()->SetBytecodeArray(bytecodes);
  }

  if (compilation_info()->SourcePositionRecordingMode() ==
      SourcePositionTableBuilder::RecordingMode::RECORD_SOURCE_POSITIONS) {
    Handle<TrustedByteArray> source_position_table =
        generator()->FinalizeSourcePositionTable(isolate);
    bytecodes->set_source_position_table(*source_position_table, kReleaseStore);
  }

  if (ShouldPrintBytecode(shared_info)) {
    StdoutStream os;
    std::unique_ptr<char[]> name =
        compilation_info()->literal()->GetDebugName();
    os << "[generated bytecode for function: " << name.get() << " ("
       << shared_info << ")]" << std::endl;
    os << "Bytecode length: " << bytecodes->length() << std::endl;
    bytecodes->Disassemble(os);
    os << std::flush;
  }

#ifdef DEBUG
  CheckAndPrintBytecodeMismatch(
      isolate, handle(Script::cast(shared_info->script()), isolate), bytecodes);
#endif

  return SUCCEEDED;
}

std::unique_ptr<UnoptimizedCompilationJob> Interpreter::NewCompilationJob(
    ParseInfo* parse_info, FunctionLiteral* literal, Handle<Script> script,
    AccountingAllocator* allocator,
    std::vector<FunctionLiteral*>* eager_inner_literals,
    LocalIsolate* local_isolate) {
  return std::make_unique<InterpreterCompilationJob>(
      parse_info, literal, script, allocator, eager_inner_literals,
      local_isolate);
}

std::unique_ptr<UnoptimizedCompilationJob>
Interpreter::NewSourcePositionCollectionJob(
    ParseInfo* parse_info, FunctionLiteral* literal,
    Handle<BytecodeArray> existing_bytecode, AccountingAllocator* allocator,
    LocalIsolate* local_isolate) {
  auto job = std::make_unique<InterpreterCompilationJob>(
      parse_info, literal, Handle<Script>(), allocator, nullptr, local_isolate);
  job->compilation_info()->SetBytecodeArray(existing_bytecode);
  return job;
}

void Interpreter::ForEachBytecode(
    const std::function<void(Bytecode, OperandScale)>& f) {
  constexpr OperandScale kOperandScales[] = {
#define VALUE(Name, _) OperandScale::k##Name,
      OPERAND_SCALE_LIST(VALUE)
#undef VALUE
  };

  for (OperandScale operand_scale : kOperandScales) {
    for (int i = 0; i < Bytecodes::kBytecodeCount; i++) {
      f(Bytecodes::FromByte(i), operand_scale);
    }
  }
}

void Interpreter::Initialize() {
  Builtins* builtins = isolate_->builtins();

  // Set the interpreter entry trampoline entry point now that builtins are
  // initialized.
  Handle<Code> code = BUILTIN_CODE(isolate_, InterpreterEntryTrampoline);
  DCHECK(builtins->is_initialized());
  DCHECK(!code->has_instruction_stream());
  interpreter_entry_trampoline_instruction_start_ = code->instruction_start();

  // Initialize the dispatch table.
  ForEachBytecode([=](Bytecode bytecode, OperandScale operand_scale) {
    Builtin builtin = BuiltinIndexFromBytecode(bytecode, operand_scale);
    Tagged<Code> handler = builtins->code(builtin);
    if (Bytecodes::BytecodeHasHandler(bytecode, operand_scale)) {
#ifdef DEBUG
      std::string builtin_name(Builtins::name(builtin));
      std::string expected_name =
          (Bytecodes::IsShortStar(bytecode)
               ? "ShortStar"
               : Bytecodes::ToString(bytecode, operand_scale, "")) +
          "Handler";
      DCHECK_EQ(expected_name, builtin_name);
#endif
    }

    SetBytecodeHandler(bytecode, operand_scale, handler);
  });
  DCHECK(IsDispatchTableInitialized());
}

bool Interpreter::IsDispatchTableInitialized() const {
  return dispatch_table_[0] != kNullAddress;
}

uintptr_t Interpreter::GetDispatchCounter(Bytecode from, Bytecode to) const {
  int from_index = Bytecodes::ToByte(from);
  int to_index = Bytecodes::ToByte(to);
  CHECK_WITH_MSG(bytecode_dispatch_counters_table_ != nullptr,
                 "Dispatch counters require building with "
                 "v8_enable_ignition_dispatch_counting");
  return bytecode_dispatch_counters_table_[from_index * kNumberOfBytecodes +
                                           to_index];
}

Handle<JSObject> Interpreter::GetDispatchCountersObject() {
  Handle<JSObject> counters_map =
      isolate_->factory()->NewJSObjectWithNullProto();

  // Output is a JSON-encoded object of objects.
  //
  // The keys on the top level object are source bytecodes,
  // and corresponding value are objects. Keys on these last are the
  // destinations of the dispatch and the value associated is a counter for
  // the correspondent source-destination dispatch chain.
  //
  // Only non-zero counters are written to file, but an entry in the top-level
  // object is always present, even if the value is empty because all counters
  // for that source are zero.

  for (int from_index = 0; from_index < kNumberOfBytecodes; ++from_index) {
    Bytecode from_bytecode = Bytecodes::FromByte(from_index);
    Handle<JSObject> counters_row =
        isolate_->factory()->NewJSObjectWithNullProto();

    for (int to_index = 0; to_index < kNumberOfBytecodes; ++to_index) {
      Bytecode to_bytecode = Bytecodes::FromByte(to_index);
      uintptr_t counter = GetDispatchCounter(from_bytecode, to_bytecode);

      if (counter > 0) {
        Handle<Object> value = isolate_->factory()->NewNumberFromSize(counter);
        JSObject::AddProperty(isolate_, counters_row,
                              Bytecodes::ToString(to_bytecode), value, NONE);
      }
    }

    JSObject::AddProperty(isolate_, counters_map,
                          Bytecodes::ToString(from_bytecode), counters_row,
                          NONE);
  }

  return counters_map;
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

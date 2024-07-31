// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/interpreter/bytecode-expectations-printer.h"

#include <iomanip>
#include <iostream>
#include <vector>

#include "include/libplatform/libplatform.h"
#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/base/logging.h"
#include "src/codegen/source-position-table.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-generator.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter-intrinsics.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/runtime/runtime.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {
namespace interpreter {

static const char* NameForNativeContextIntrinsicIndex(uint32_t idx) {
  switch (idx) {
#define COMPARE_NATIVE_CONTEXT_INTRINSIC_IDX(NAME, Type, name) \
  case Context::NAME:                                          \
    return #name;

    NATIVE_CONTEXT_INTRINSIC_FUNCTIONS(COMPARE_NATIVE_CONTEXT_INTRINSIC_IDX)

    default:
      break;
  }

  return "UnknownIntrinsicIndex";
}

// static
const char* const BytecodeExpectationsPrinter::kDefaultTopFunctionName =
    "__genbckexp_wrapper__";
const char* const BytecodeExpectationsPrinter::kIndent = "  ";

v8::Local<v8::String> BytecodeExpectationsPrinter::V8StringFromUTF8(
    const char* data) const {
  return v8::String::NewFromUtf8(isolate_, data).ToLocalChecked();
}

std::string BytecodeExpectationsPrinter::WrapCodeInFunction(
    const char* function_name, const std::string& function_body) const {
  std::ostringstream program_stream;
  program_stream << "function " << function_name << "() {" << function_body
                 << "}\n"
                 << function_name << "();";

  return program_stream.str();
}

v8::Local<v8::Script> BytecodeExpectationsPrinter::CompileScript(
    const char* program) const {
  v8::Local<v8::String> source = V8StringFromUTF8(program);
  return v8::Script::Compile(isolate_->GetCurrentContext(), source)
      .ToLocalChecked();
}

v8::Local<v8::Module> BytecodeExpectationsPrinter::CompileModule(
    const char* program) const {
  ScriptOrigin origin(Local<v8::Value>(), 0, 0, false, -1, Local<v8::Value>(),
                      false, false, true);
  v8::ScriptCompiler::Source source(V8StringFromUTF8(program), origin);
  return v8::ScriptCompiler::CompileModule(isolate_, &source).ToLocalChecked();
}

void BytecodeExpectationsPrinter::Run(v8::Local<v8::Script> script) const {
  MaybeLocal<Value> result = script->Run(isolate_->GetCurrentContext());
  USE(result);
}

i::Handle<v8::internal::BytecodeArray>
BytecodeExpectationsPrinter::GetBytecodeArrayForGlobal(
    const char* global_name) const {
  const v8::Local<v8::Context>& context = isolate_->GetCurrentContext();
  v8::Local<v8::String> v8_global_name = V8StringFromUTF8(global_name);
  v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(
      context->Global()->Get(context, v8_global_name).ToLocalChecked());
  i::Handle<i::JSFunction> js_function =
      i::Handle<i::JSFunction>::cast(v8::Utils::OpenHandle(*function));

  i::Handle<i::BytecodeArray> bytecodes = i::handle(
      js_function->shared()->GetBytecodeArray(i_isolate()), i_isolate());

  return bytecodes;
}

i::Handle<i::BytecodeArray>
BytecodeExpectationsPrinter::GetBytecodeArrayForModule(
    v8::Local<v8::Module> module) const {
  i::Handle<i::Module> i_module = v8::Utils::OpenHandle(*module);
  return i::handle(SharedFunctionInfo::cast(
                       Handle<i::SourceTextModule>::cast(i_module)->code())
                       ->GetBytecodeArray(i_isolate()),
                   i_isolate());
}

i::Handle<i::BytecodeArray>
BytecodeExpectationsPrinter::GetBytecodeArrayForScript(
    v8::Local<v8::Script> script) const {
  i::Handle<i::JSFunction> js_function = v8::Utils::OpenHandle(*script);
  return i::handle(js_function->shared()->GetBytecodeArray(i_isolate()),
                   i_isolate());
}

i::Handle<i::BytecodeArray>
BytecodeExpectationsPrinter::GetBytecodeArrayOfCallee(
    const char* source_code) const {
  Local<v8::Context> context = isolate_->GetCurrentContext();
  Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromUtf8(isolate_, source_code).ToLocalChecked())
          .ToLocalChecked();
  i::Handle<i::Object> i_object =
      v8::Utils::OpenHandle(*script->Run(context).ToLocalChecked());
  i::Handle<i::JSFunction> js_function =
      i::Handle<i::JSFunction>::cast(i_object);
  CHECK(js_function->shared()->HasBytecodeArray());
  return i::handle(js_function->shared()->GetBytecodeArray(i_isolate()),
                   i_isolate());
}

void BytecodeExpectationsPrinter::PrintEscapedString(
    std::ostream* stream, const std::string& string) const {
  for (char c : string) {
    switch (c) {
      case '"':
        *stream << "\\\"";
        break;
      case '\\':
        *stream << "\\\\";
        break;
      default:
        *stream << c;
        break;
    }
  }
}

void BytecodeExpectationsPrinter::PrintBytecodeOperand(
    std::ostream* stream, const BytecodeArrayIterator& bytecode_iterator,
    const Bytecode& bytecode, int op_index, int parameter_count) const {
  OperandType op_type = Bytecodes::GetOperandType(bytecode, op_index);
  OperandSize op_size = Bytecodes::GetOperandSize(
      bytecode, op_index, bytecode_iterator.current_operand_scale());

  const char* size_tag;
  switch (op_size) {
    case OperandSize::kByte:
      size_tag = "8";
      break;
    case OperandSize::kShort:
      size_tag = "16";
      break;
    case OperandSize::kQuad:
      size_tag = "32";
      break;
    default:
      UNREACHABLE();
  }

  if (Bytecodes::IsRegisterOperandType(op_type)) {
    Register register_value = bytecode_iterator.GetRegisterOperand(op_index);
    *stream << 'R';
    if (op_size != OperandSize::kByte) *stream << size_tag;
    if (register_value.is_current_context()) {
      *stream << "(context)";
    } else if (register_value.is_function_closure()) {
      *stream << "(closure)";
    } else if (register_value.is_parameter()) {
      int parameter_index = register_value.ToParameterIndex();
      if (parameter_index == 0) {
        *stream << "(this)";
      } else {
        *stream << "(arg" << (parameter_index - 1) << ')';
      }
    } else {
      *stream << '(' << register_value.index() << ')';
    }
  } else {
    switch (op_type) {
      case OperandType::kFlag8:
        *stream << 'U' << size_tag << '(';
        *stream << bytecode_iterator.GetFlag8Operand(op_index);
        break;
      case OperandType::kFlag16:
        *stream << 'U' << size_tag << '(';
        *stream << bytecode_iterator.GetFlag16Operand(op_index);
        break;
      case OperandType::kIdx: {
        *stream << 'U' << size_tag << '(';
        *stream << bytecode_iterator.GetIndexOperand(op_index);
        break;
      }
      case OperandType::kUImm:
        *stream << 'U' << size_tag << '(';
        *stream << bytecode_iterator.GetUnsignedImmediateOperand(op_index);
        break;
      case OperandType::kImm:
        *stream << 'I' << size_tag << '(';
        *stream << bytecode_iterator.GetImmediateOperand(op_index);
        break;
      case OperandType::kRegCount:
        *stream << 'U' << size_tag << '(';
        *stream << bytecode_iterator.GetRegisterCountOperand(op_index);
        break;
      case OperandType::kRuntimeId: {
        *stream << 'U' << size_tag << '(';
        Runtime::FunctionId id =
            bytecode_iterator.GetRuntimeIdOperand(op_index);
        *stream << "Runtime::k" << i::Runtime::FunctionForId(id)->name;
        break;
      }
      case OperandType::kIntrinsicId: {
        *stream << 'U' << size_tag << '(';
        Runtime::FunctionId id =
            bytecode_iterator.GetIntrinsicIdOperand(op_index);
        *stream << "Runtime::k" << i::Runtime::FunctionForId(id)->name;
        break;
      }
      case OperandType::kNativeContextIndex: {
        *stream << 'U' << size_tag << '(';
        uint32_t idx = bytecode_iterator.GetNativeContextIndexOperand(op_index);
        *stream << "%" << NameForNativeContextIntrinsicIndex(idx);
        break;
      }
      default:
        UNREACHABLE();
    }

    *stream << ')';
  }
}

void BytecodeExpectationsPrinter::PrintBytecode(
    std::ostream* stream, const BytecodeArrayIterator& bytecode_iterator,
    int parameter_count) const {
  Bytecode bytecode = bytecode_iterator.current_bytecode();
  OperandScale operand_scale = bytecode_iterator.current_operand_scale();
  if (Bytecodes::OperandScaleRequiresPrefixBytecode(operand_scale)) {
    Bytecode prefix = Bytecodes::OperandScaleToPrefixBytecode(operand_scale);
    *stream << "B(" << Bytecodes::ToString(prefix) << "), ";
  }
  *stream << "B(" << Bytecodes::ToString(bytecode) << ')';
  int operands_count = Bytecodes::NumberOfOperands(bytecode);
  for (int op_index = 0; op_index < operands_count; ++op_index) {
    *stream << ", ";
    PrintBytecodeOperand(stream, bytecode_iterator, bytecode, op_index,
                         parameter_count);
  }
}

void BytecodeExpectationsPrinter::PrintSourcePosition(
    std::ostream* stream, SourcePositionTableIterator* source_iterator,
    int bytecode_offset) const {
  static const size_t kPositionWidth = 4;
  if (!source_iterator->done() &&
      source_iterator->code_offset() == bytecode_offset) {
    *stream << "/* " << std::setw(kPositionWidth)
            << source_iterator->source_position().ScriptOffset();
    if (source_iterator->is_statement()) {
      *stream << " S> */ ";
    } else {
      *stream << " E> */ ";
    }
    source_iterator->Advance();
  } else {
    *stream << "   " << std::setw(kPositionWidth) << ' ' << "       ";
  }
}

void BytecodeExpectationsPrinter::PrintV8String(
    std::ostream* stream, i::Tagged<i::String> string) const {
  *stream << '"';
  for (int i = 0, length = string->length(); i < length; ++i) {
    *stream << i::AsEscapedUC16ForJSON(string->Get(i));
  }
  *stream << '"';
}

void BytecodeExpectationsPrinter::PrintConstant(
    std::ostream* stream, i::Handle<i::Object> constant) const {
  if (IsSmi(*constant)) {
    *stream << "Smi [";
    i::Smi::SmiPrint(i::Smi::cast(*constant), *stream);
    *stream << "]";
  } else {
    *stream << i::HeapObject::cast(*constant)->map()->instance_type();
    if (IsHeapNumber(*constant)) {
      *stream << " [";
      i::HeapNumber::cast(*constant)->HeapNumberShortPrint(*stream);
      *stream << "]";
    } else if (IsString(*constant)) {
      *stream << " [";
      PrintV8String(stream, i::String::cast(*constant));
      *stream << "]";
    }
  }
}

void BytecodeExpectationsPrinter::PrintFrameSize(
    std::ostream* stream, i::Handle<i::BytecodeArray> bytecode_array) const {
  int32_t frame_size = bytecode_array->frame_size();

  DCHECK(IsAligned(frame_size, kSystemPointerSize));
  *stream << "frame size: " << frame_size / kSystemPointerSize
          << "\nparameter count: " << bytecode_array->parameter_count() << '\n';
}

void BytecodeExpectationsPrinter::PrintBytecodeSequence(
    std::ostream* stream, i::Handle<i::BytecodeArray> bytecode_array) const {
  *stream << "bytecode array length: " << bytecode_array->length()
          << "\nbytecodes: [\n";

  SourcePositionTableIterator source_iterator(
      bytecode_array->SourcePositionTable());
  BytecodeArrayIterator bytecode_iterator(bytecode_array);
  for (; !bytecode_iterator.done(); bytecode_iterator.Advance()) {
    *stream << kIndent;
    PrintSourcePosition(stream, &source_iterator,
                        bytecode_iterator.current_offset());
    PrintBytecode(stream, bytecode_iterator, bytecode_array->parameter_count());
    *stream << ",\n";
  }
  *stream << "]\n";
}

void BytecodeExpectationsPrinter::PrintConstantPool(
    std::ostream* stream, i::Tagged<i::TrustedFixedArray> constant_pool) const {
  *stream << "constant pool: [\n";
  int num_constants = constant_pool->length();
  if (num_constants > 0) {
    for (int i = 0; i < num_constants; ++i) {
      *stream << kIndent;
      PrintConstant(stream, handle(constant_pool->get(i), i_isolate()));
      *stream << ",\n";
    }
  }
  *stream << "]\n";
}

void BytecodeExpectationsPrinter::PrintCodeSnippet(
    std::ostream* stream, const std::string& body) const {
  *stream << "snippet: \"\n";
  std::stringstream body_stream(body);
  std::string body_line;
  while (std::getline(body_stream, body_line)) {
    *stream << kIndent;
    PrintEscapedString(stream, body_line);
    *stream << '\n';
  }
  *stream << "\"\n";
}

void BytecodeExpectationsPrinter::PrintHandlers(
    std::ostream* stream, i::Handle<i::BytecodeArray> bytecode_array) const {
  *stream << "handlers: [\n";
  HandlerTable table(*bytecode_array);
  for (int i = 0, num_entries = table.NumberOfRangeEntries(); i < num_entries;
       ++i) {
    *stream << "  [" << table.GetRangeStart(i) << ", " << table.GetRangeEnd(i)
            << ", " << table.GetRangeHandler(i) << "],\n";
  }
  *stream << "]\n";
}

void BytecodeExpectationsPrinter::PrintBytecodeArray(
    std::ostream* stream, i::Handle<i::BytecodeArray> bytecode_array) const {
  PrintFrameSize(stream, bytecode_array);
  PrintBytecodeSequence(stream, bytecode_array);
  PrintConstantPool(stream, bytecode_array->constant_pool());
  PrintHandlers(stream, bytecode_array);
}

void BytecodeExpectationsPrinter::PrintExpectation(
    std::ostream* stream, const std::string& snippet) const {
  std::string source_code =
      wrap_ ? WrapCodeInFunction(test_function_name_.c_str(), snippet)
            : snippet;

  i::v8_flags.compilation_cache = false;
  i::Handle<i::BytecodeArray> bytecode_array;
  if (module_) {
    CHECK(top_level_ && !wrap_);
    v8::Local<v8::Module> module = CompileModule(source_code.c_str());
    bytecode_array = GetBytecodeArrayForModule(module);
  } else if (print_callee_) {
    bytecode_array = GetBytecodeArrayOfCallee(source_code.c_str());
  } else {
    v8::Local<v8::Script> script = CompileScript(source_code.c_str());
    if (top_level_) {
      bytecode_array = GetBytecodeArrayForScript(script);
    } else {
      Run(script);
      bytecode_array = GetBytecodeArrayForGlobal(test_function_name_.c_str());
    }
  }

  *stream << "---\n";
  PrintCodeSnippet(stream, snippet);
  PrintBytecodeArray(stream, bytecode_array);
  *stream << '\n';
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

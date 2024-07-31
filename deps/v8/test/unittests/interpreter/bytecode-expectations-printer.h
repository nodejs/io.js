// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_UNITTESTS_INTERPRETER_BYTECODE_EXPECTATIONS_PRINTER_H_
#define TEST_UNITTESTS_INTERPRETER_BYTECODE_EXPECTATIONS_PRINTER_H_

#include <iostream>
#include <string>
#include <vector>

#include "include/v8-local-handle.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/objects.h"

namespace v8 {

class Isolate;
class Script;
class Module;

namespace internal {

class BytecodeArray;
class SourcePositionTableIterator;

namespace interpreter {

class BytecodeArrayIterator;

class BytecodeExpectationsPrinter final {
 public:
  explicit BytecodeExpectationsPrinter(v8::Isolate* i)
      : isolate_(i),
        module_(false),
        wrap_(true),
        top_level_(false),
        print_callee_(false),
        test_function_name_(kDefaultTopFunctionName) {}

  void PrintExpectation(std::ostream* stream, const std::string& snippet) const;

  void set_module(bool module) { module_ = module; }
  bool module() const { return module_; }

  void set_wrap(bool wrap) { wrap_ = wrap; }
  bool wrap() const { return wrap_; }

  void set_top_level(bool top_level) { top_level_ = top_level; }
  bool top_level() const { return top_level_; }

  void set_print_callee(bool print_callee) { print_callee_ = print_callee; }
  bool print_callee() { return print_callee_; }

  void set_test_function_name(const std::string& test_function_name) {
    test_function_name_ = test_function_name;
  }
  std::string test_function_name() const { return test_function_name_; }

 private:
  void PrintEscapedString(std::ostream* stream,
                          const std::string& string) const;
  void PrintBytecodeOperand(std::ostream* stream,
                            const BytecodeArrayIterator& bytecode_iterator,
                            const Bytecode& bytecode, int op_index,
                            int parameter_count) const;
  void PrintBytecode(std::ostream* stream,
                     const BytecodeArrayIterator& bytecode_iterator,
                     int parameter_count) const;
  void PrintSourcePosition(std::ostream* stream,
                           SourcePositionTableIterator* source_iterator,
                           int bytecode_offset) const;
  void PrintV8String(std::ostream* stream, i::Tagged<i::String> string) const;
  void PrintConstant(std::ostream* stream, i::Handle<i::Object> constant) const;
  void PrintFrameSize(std::ostream* stream,
                      i::Handle<i::BytecodeArray> bytecode_array) const;
  void PrintBytecodeSequence(std::ostream* stream,
                             i::Handle<i::BytecodeArray> bytecode_array) const;
  void PrintConstantPool(std::ostream* stream,
                         i::Tagged<i::TrustedFixedArray> constant_pool) const;
  void PrintCodeSnippet(std::ostream* stream, const std::string& body) const;
  void PrintBytecodeArray(std::ostream* stream,
                          i::Handle<i::BytecodeArray> bytecode_array) const;
  void PrintHandlers(std::ostream* stream,
                     i::Handle<i::BytecodeArray> bytecode_array) const;

  v8::Local<v8::String> V8StringFromUTF8(const char* data) const;
  std::string WrapCodeInFunction(const char* function_name,
                                 const std::string& function_body) const;

  v8::Local<v8::Script> CompileScript(const char* program) const;
  v8::Local<v8::Module> CompileModule(const char* program) const;
  void Run(v8::Local<v8::Script> script) const;
  i::Handle<i::BytecodeArray> GetBytecodeArrayForGlobal(
      const char* global_name) const;
  i::Handle<v8::internal::BytecodeArray> GetBytecodeArrayForModule(
      v8::Local<v8::Module> module) const;
  i::Handle<v8::internal::BytecodeArray> GetBytecodeArrayForScript(
      v8::Local<v8::Script> script) const;
  i::Handle<i::BytecodeArray> GetBytecodeArrayOfCallee(
      const char* source_code) const;

  i::Isolate* i_isolate() const {
    return reinterpret_cast<i::Isolate*>(isolate_);
  }

  v8::Isolate* isolate_;
  bool module_;
  bool wrap_;
  bool top_level_;
  bool print_callee_;
  std::string test_function_name_;

  static const char* const kDefaultTopFunctionName;
  static const char* const kIndent;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // TEST_UNITTESTS_INTERPRETER_BYTECODE_EXPECTATIONS_PRINTER_H_

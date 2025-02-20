// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_PRETTYPRINTER_H_
#define V8_AST_PRETTYPRINTER_H_

#include "src/ast/ast.h"
#include "src/base/compiler-specific.h"
#include "src/execution/isolate.h"
#include "src/objects/function-kind.h"
#include "src/strings/string-builder.h"

namespace v8 {
namespace internal {

class CallPrinter final : public AstVisitor<CallPrinter> {
 public:
  enum class SpreadErrorInArgsHint { kErrorInArgs, kNoErrorInArgs };

  explicit CallPrinter(Isolate* isolate, bool is_user_js,
                       SpreadErrorInArgsHint error_in_spread_args =
                           SpreadErrorInArgsHint::kNoErrorInArgs);
  ~CallPrinter();

  // The following routine prints the node with position |position| into a
  // string.
  DirectHandle<String> Print(FunctionLiteral* program, int position);
  enum class ErrorHint {
    kNone,
    kNormalIterator,
    kAsyncIterator,
    kCallAndNormalIterator,
    kCallAndAsyncIterator
  };

  ErrorHint GetErrorHint() const;
  Expression* spread_arg() const { return spread_arg_; }
  ObjectLiteralProperty* destructuring_prop() const {
    return destructuring_prop_;
  }
  Assignment* destructuring_assignment() const {
    return destructuring_assignment_;
  }

// Individual nodes
#define DECLARE_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

 private:
  void Print(char c);
  void Print(const char* str);
  void Print(DirectHandle<String> str);

  void Find(AstNode* node, bool print = false);

  Isolate* isolate_;
  int num_prints_;
  IncrementalStringBuilder builder_;
  int position_;  // position of ast node to print
  bool found_;
  bool done_;
  bool is_user_js_;
  bool is_iterator_error_;
  bool is_async_iterator_error_;
  bool is_call_error_;
  SpreadErrorInArgsHint error_in_spread_args_;
  ObjectLiteralProperty* destructuring_prop_;
  Assignment* destructuring_assignment_;
  Expression* spread_arg_;
  FunctionKind function_kind_;
  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();

 protected:
  void PrintLiteral(DirectHandle<Object> value, bool quote);
  void PrintLiteral(const AstRawString* value, bool quote);
  void FindStatements(const ZonePtrList<Statement>* statements);
  void FindArguments(const ZonePtrList<Expression>* arguments);
};


#ifdef DEBUG

class AstPrinter final : public AstVisitor<AstPrinter> {
 public:
  explicit AstPrinter(uintptr_t stack_limit);
  ~AstPrinter();

  // The following routines print a node into a string.
  // The result string is alive as long as the AstPrinter is alive.
  const char* Print(AstNode* node);
  const char* PrintProgram(FunctionLiteral* program);

  void PRINTF_FORMAT(2, 3) Print(const char* format, ...);

  // Print a node to stdout.
  static void PrintOut(Isolate* isolate, AstNode* node);

  // Individual nodes
#define DECLARE_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

 private:
  friend class IndentedScope;

  void Init();

  void PrintLabels(ZonePtrList<const AstRawString>* labels);
  void PrintLiteral(const AstRawString* value, bool quote);
  void PrintLiteral(const AstConsString* value, bool quote);
  void PrintLiteral(Literal* literal, bool quote);
  void PrintIndented(const char* txt);
  void PrintIndentedVisit(const char* s, AstNode* node);

  void PrintStatements(const ZonePtrList<Statement>* statements);
  void PrintDeclarations(Declaration::List* declarations);
  void PrintParameters(DeclarationScope* scope);
  void PrintArguments(const ZonePtrList<Expression>* arguments);
  void PrintCaseClause(CaseClause* clause);
  void PrintLiteralIndented(const char* info, Literal* literal, bool quote);
  void PrintLiteralIndented(const char* info, const AstRawString* value,
                            bool quote);
  void PrintLiteralIndented(const char* info, const AstConsString* value,
                            bool quote);
  void PrintLiteralWithModeIndented(const char* info, Variable* var,
                                    const AstRawString* value);
  void PrintLabelsIndented(ZonePtrList<const AstRawString>* labels,
                           const char* prefix = "");
  void PrintObjectProperties(
      const ZonePtrList<ObjectLiteral::Property>* properties);
  void PrintClassProperty(ClassLiteral::Property* property);
  void PrintClassProperties(
      const ZonePtrList<ClassLiteral::Property>* properties);
  void PrintClassStaticElements(
      const ZonePtrList<ClassLiteral::StaticElement>* static_elements);

  void inc_indent() { indent_++; }
  void dec_indent() { indent_--; }

  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();

  char* output_;  // output string buffer
  int size_;      // output_ size
  int pos_;       // current printing position
  int indent_;
};

#endif  // DEBUG

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_PRETTYPRINTER_H_

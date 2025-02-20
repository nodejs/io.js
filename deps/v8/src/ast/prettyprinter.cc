// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/prettyprinter.h"

#include <stdarg.h>

#include "src/ast/ast-value-factory.h"
#include "src/ast/scopes.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/objects/objects-inl.h"
#include "src/regexp/regexp-flags.h"
#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

CallPrinter::CallPrinter(Isolate* isolate, bool is_user_js,
                         SpreadErrorInArgsHint error_in_spread_args)
    : builder_(isolate) {
  isolate_ = isolate;
  position_ = 0;
  num_prints_ = 0;
  found_ = false;
  done_ = false;
  is_call_error_ = false;
  is_iterator_error_ = false;
  is_async_iterator_error_ = false;
  destructuring_prop_ = nullptr;
  destructuring_assignment_ = nullptr;
  is_user_js_ = is_user_js;
  error_in_spread_args_ = error_in_spread_args;
  spread_arg_ = nullptr;
  function_kind_ = FunctionKind::kNormalFunction;
  InitializeAstVisitor(isolate);
}

CallPrinter::~CallPrinter() = default;

CallPrinter::ErrorHint CallPrinter::GetErrorHint() const {
  if (is_call_error_) {
    if (is_iterator_error_) return ErrorHint::kCallAndNormalIterator;
    if (is_async_iterator_error_) return ErrorHint::kCallAndAsyncIterator;
  } else {
    if (is_iterator_error_) return ErrorHint::kNormalIterator;
    if (is_async_iterator_error_) return ErrorHint::kAsyncIterator;
  }
  return ErrorHint::kNone;
}

DirectHandle<String> CallPrinter::Print(FunctionLiteral* program,
                                        int position) {
  num_prints_ = 0;
  position_ = position;
  Find(program);
  return builder_.Finish().ToHandleChecked();
}

void CallPrinter::Find(AstNode* node, bool print) {
  if (found_) {
    if (print) {
      int prev_num_prints = num_prints_;
      Visit(node);
      if (prev_num_prints != num_prints_) return;
    }
    Print("(intermediate value)");
  } else {
    Visit(node);
  }
}

void CallPrinter::Print(char c) {
  if (!found_ || done_) return;
  num_prints_++;
  builder_.AppendCharacter(c);
}

void CallPrinter::Print(const char* str) {
  if (!found_ || done_) return;
  num_prints_++;
  builder_.AppendCString(str);
}

void CallPrinter::Print(DirectHandle<String> str) {
  if (!found_ || done_) return;
  num_prints_++;
  builder_.AppendString(str);
}

void CallPrinter::VisitBlock(Block* node) {
  FindStatements(node->statements());
}


void CallPrinter::VisitVariableDeclaration(VariableDeclaration* node) {}


void CallPrinter::VisitFunctionDeclaration(FunctionDeclaration* node) {}


void CallPrinter::VisitExpressionStatement(ExpressionStatement* node) {
  Find(node->expression());
}


void CallPrinter::VisitEmptyStatement(EmptyStatement* node) {}


void CallPrinter::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* node) {
  Find(node->statement());
}


void CallPrinter::VisitIfStatement(IfStatement* node) {
  Find(node->condition());
  Find(node->then_statement());
  if (node->HasElseStatement()) {
    Find(node->else_statement());
  }
}


void CallPrinter::VisitContinueStatement(ContinueStatement* node) {}


void CallPrinter::VisitBreakStatement(BreakStatement* node) {}


void CallPrinter::VisitReturnStatement(ReturnStatement* node) {
  Find(node->expression());
}


void CallPrinter::VisitWithStatement(WithStatement* node) {
  Find(node->expression());
  Find(node->statement());
}


void CallPrinter::VisitSwitchStatement(SwitchStatement* node) {
  Find(node->tag());
  for (CaseClause* clause : *node->cases()) {
    if (!clause->is_default()) Find(clause->label());
    FindStatements(clause->statements());
  }
}


void CallPrinter::VisitDoWhileStatement(DoWhileStatement* node) {
  Find(node->body());
  Find(node->cond());
}


void CallPrinter::VisitWhileStatement(WhileStatement* node) {
  Find(node->cond());
  Find(node->body());
}


void CallPrinter::VisitForStatement(ForStatement* node) {
  if (node->init() != nullptr) {
    Find(node->init());
  }
  if (node->cond() != nullptr) Find(node->cond());
  if (node->next() != nullptr) Find(node->next());
  Find(node->body());
}


void CallPrinter::VisitForInStatement(ForInStatement* node) {
  Find(node->each());
  Find(node->subject());
  Find(node->body());
}


void CallPrinter::VisitForOfStatement(ForOfStatement* node) {
  Find(node->each());

  // Check the subject's position in case there was a GetIterator error.
  bool was_found = false;
  if (node->subject()->position() == position_) {
    is_async_iterator_error_ = node->type() == IteratorType::kAsync;
    is_iterator_error_ = !is_async_iterator_error_;
    was_found = !found_;
    if (was_found) {
      found_ = true;
    }
  }
  Find(node->subject(), true);
  if (was_found) {
    done_ = true;
    found_ = false;
  }

  Find(node->body());
}


void CallPrinter::VisitTryCatchStatement(TryCatchStatement* node) {
  Find(node->try_block());
  Find(node->catch_block());
}


void CallPrinter::VisitTryFinallyStatement(TryFinallyStatement* node) {
  Find(node->try_block());
  Find(node->finally_block());
}


void CallPrinter::VisitDebuggerStatement(DebuggerStatement* node) {}


void CallPrinter::VisitFunctionLiteral(FunctionLiteral* node) {
  FunctionKind last_function_kind = function_kind_;
  function_kind_ = node->kind();
  FindStatements(node->body());
  function_kind_ = last_function_kind;
}


void CallPrinter::VisitClassLiteral(ClassLiteral* node) {
  if (node->extends()) Find(node->extends());
  for (int i = 0; i < node->public_members()->length(); i++) {
    Find(node->public_members()->at(i)->value());
  }
  for (int i = 0; i < node->private_members()->length(); i++) {
    Find(node->private_members()->at(i)->value());
  }
}

void CallPrinter::VisitInitializeClassMembersStatement(
    InitializeClassMembersStatement* node) {
  for (int i = 0; i < node->fields()->length(); i++) {
    Find(node->fields()->at(i)->value());
  }
}

void CallPrinter::VisitInitializeClassStaticElementsStatement(
    InitializeClassStaticElementsStatement* node) {
  for (int i = 0; i < node->elements()->length(); i++) {
    ClassLiteral::StaticElement* element = node->elements()->at(i);
    if (element->kind() == ClassLiteral::StaticElement::PROPERTY) {
      Find(element->property()->value());
    } else {
      Find(element->static_block());
    }
  }
}

void CallPrinter::VisitAutoAccessorGetterBody(AutoAccessorGetterBody* node) {}

void CallPrinter::VisitAutoAccessorSetterBody(AutoAccessorSetterBody* node) {}

void CallPrinter::VisitNativeFunctionLiteral(NativeFunctionLiteral* node) {}

void CallPrinter::VisitConditionalChain(ConditionalChain* node) {
  for (size_t i = 0; i < node->conditional_chain_length(); ++i) {
    Find(node->condition_at(i));
    Find(node->then_expression_at(i));
  }
  Find(node->else_expression());
}

void CallPrinter::VisitConditional(Conditional* node) {
  Find(node->condition());
  Find(node->then_expression());
  Find(node->else_expression());
}


void CallPrinter::VisitLiteral(Literal* node) {
  // TODO(adamk): Teach Literal how to print its values without
  // allocating on the heap.
  PrintLiteral(node->BuildValue(isolate_), true);
}


void CallPrinter::VisitRegExpLiteral(RegExpLiteral* node) {
  Print("/");
  PrintLiteral(node->pattern(), false);
  Print("/");
#define V(Lower, Camel, LowerCamel, Char, Bit) \
  if (node->flags() & RegExp::k##Camel) Print(Char);
  REGEXP_FLAG_LIST(V)
#undef V
}


void CallPrinter::VisitObjectLiteral(ObjectLiteral* node) {
  Print("{");
  for (int i = 0; i < node->properties()->length(); i++) {
    Find(node->properties()->at(i)->value());
  }
  Print("}");
}


void CallPrinter::VisitArrayLiteral(ArrayLiteral* node) {
  Print("[");
  for (int i = 0; i < node->values()->length(); i++) {
    if (i != 0) Print(",");
    Expression* subexpr = node->values()->at(i);
    Spread* spread = subexpr->AsSpread();
    if (spread != nullptr && !found_ &&
        position_ == spread->expression()->position()) {
      found_ = true;
      is_iterator_error_ = true;
      Find(spread->expression(), true);
      done_ = true;
      return;
    }
    Find(subexpr, true);
  }
  Print("]");
}


void CallPrinter::VisitVariableProxy(VariableProxy* node) {
  if (is_user_js_) {
    PrintLiteral(node->name(), false);
  } else {
    // Variable names of non-user code are meaningless due to minification.
    Print("(var)");
  }
}


void CallPrinter::VisitAssignment(Assignment* node) {
  bool was_found = false;
  if (node->target()->IsObjectLiteral()) {
    ObjectLiteral* target = node->target()->AsObjectLiteral();
    if (target->position() == position_) {
      was_found = !found_;
      found_ = true;
      destructuring_assignment_ = node;
    } else {
      for (ObjectLiteralProperty* prop : *target->properties()) {
        if (prop->value()->position() == position_) {
          was_found = !found_;
          found_ = true;
          destructuring_prop_ = prop;
          destructuring_assignment_ = node;
          break;
        }
      }
    }
  }
  if (!was_found) {
    if (found_) {
      Find(node->target(), true);
      return;
    }
    Find(node->target());
    if (node->target()->IsArrayLiteral()) {
      // Special case the visit for destructuring array assignment.
      if (node->value()->position() == position_) {
        is_iterator_error_ = true;
        was_found = !found_;
        found_ = true;
      }
      Find(node->value(), true);
    } else {
      Find(node->value());
    }
  } else {
    Find(node->value(), true);
  }

  if (was_found) {
    done_ = true;
    found_ = false;
  }
}

void CallPrinter::VisitCompoundAssignment(CompoundAssignment* node) {
  VisitAssignment(node);
}

void CallPrinter::VisitYield(Yield* node) { Find(node->expression()); }

void CallPrinter::VisitYieldStar(YieldStar* node) {
  if (!found_ && position_ == node->expression()->position()) {
    found_ = true;
    if (IsAsyncFunction(function_kind_))
      is_async_iterator_error_ = true;
    else
      is_iterator_error_ = true;
    Print("yield* ");
  }
  Find(node->expression());
}

void CallPrinter::VisitAwait(Await* node) { Find(node->expression()); }

void CallPrinter::VisitThrow(Throw* node) { Find(node->exception()); }

void CallPrinter::VisitOptionalChain(OptionalChain* node) {
  Find(node->expression());
}

void CallPrinter::VisitProperty(Property* node) {
  Expression* key = node->key();
  Literal* literal = key->AsLiteral();
  if (literal != nullptr &&
      IsInternalizedString(*literal->BuildValue(isolate_))) {
    Find(node->obj(), true);
    if (node->is_optional_chain_link()) {
      Print("?");
    }
    Print(".");
    // TODO(adamk): Teach Literal how to print its values without
    // allocating on the heap.
    PrintLiteral(literal->BuildValue(isolate_), false);
  } else {
    Find(node->obj(), true);
    if (node->is_optional_chain_link()) {
      Print("?.");
    }
    Print("[");
    Find(key, true);
    Print("]");
  }
}

void CallPrinter::VisitCall(Call* node) {
  bool was_found = false;
  if (node->position() == position_) {
    if (error_in_spread_args_ == SpreadErrorInArgsHint::kErrorInArgs &&
        !node->arguments()->is_empty()) {
      if (const Spread* spread = node->arguments()->last()->AsSpread()) {
        found_ = true;
        spread_arg_ = spread->expression();
        Find(spread_arg_, true);

        done_ = true;
        found_ = false;
        return;
      }
    }

    is_call_error_ = true;
    was_found = !found_;
  }

  if (was_found) {
    // Bail out if the error is caused by a direct call to a variable in
    // non-user JS code. The variable name is meaningless due to minification.
    if (!is_user_js_ && node->expression()->IsVariableProxy()) {
      done_ = true;
      return;
    }
    found_ = true;
  }
  Find(node->expression(), true);
  if (!was_found && !is_iterator_error_) Print("(...)");
  FindArguments(node->arguments());
  if (was_found) {
    done_ = true;
    found_ = false;
  }
}


void CallPrinter::VisitCallNew(CallNew* node) {
  bool was_found = false;
  if (node->position() == position_) {
    if (error_in_spread_args_ == SpreadErrorInArgsHint::kErrorInArgs &&
        !node->arguments()->is_empty()) {
      if (const Spread* spread = node->arguments()->last()->AsSpread()) {
        found_ = true;
        spread_arg_ = spread->expression();
        Find(spread_arg_, true);

        done_ = true;
        found_ = false;
        return;
      }
    }

    is_call_error_ = true;
    was_found = !found_;
  }
  if (was_found) {
    // Bail out if the error is caused by a direct call to a variable in
    // non-user JS code. The variable name is meaningless due to minification.
    if (!is_user_js_ && node->expression()->IsVariableProxy()) {
      done_ = true;
      return;
    }
    found_ = true;
  }
  Find(node->expression(), was_found || is_iterator_error_);
  FindArguments(node->arguments());
  if (was_found) {
    done_ = true;
    found_ = false;
  }
}


void CallPrinter::VisitCallRuntime(CallRuntime* node) {
  FindArguments(node->arguments());
}

void CallPrinter::VisitSuperCallForwardArgs(SuperCallForwardArgs* node) {
  Find(node->expression(), true);
  Print("(...forwarded args...)");
}

void CallPrinter::VisitUnaryOperation(UnaryOperation* node) {
  Token::Value op = node->op();
  bool needsSpace =
      op == Token::kDelete || op == Token::kTypeOf || op == Token::kVoid;
  Print("(");
  Print(Token::String(op));
  if (needsSpace) Print(" ");
  Find(node->expression(), true);
  Print(")");
}


void CallPrinter::VisitCountOperation(CountOperation* node) {
  Print("(");
  if (node->is_prefix()) Print(Token::String(node->op()));
  Find(node->expression(), true);
  if (node->is_postfix()) Print(Token::String(node->op()));
  Print(")");
}


void CallPrinter::VisitBinaryOperation(BinaryOperation* node) {
  Print("(");
  Find(node->left(), true);
  Print(" ");
  Print(Token::String(node->op()));
  Print(" ");
  Find(node->right(), true);
  Print(")");
}

void CallPrinter::VisitNaryOperation(NaryOperation* node) {
  Print("(");
  Find(node->first(), true);
  for (size_t i = 0; i < node->subsequent_length(); ++i) {
    Print(" ");
    Print(Token::String(node->op()));
    Print(" ");
    Find(node->subsequent(i), true);
  }
  Print(")");
}

void CallPrinter::VisitCompareOperation(CompareOperation* node) {
  Print("(");
  Find(node->left(), true);
  Print(" ");
  Print(Token::String(node->op()));
  Print(" ");
  Find(node->right(), true);
  Print(")");
}


void CallPrinter::VisitSpread(Spread* node) {
  Print("(...");
  Find(node->expression(), true);
  Print(")");
}

void CallPrinter::VisitEmptyParentheses(EmptyParentheses* node) {
  UNREACHABLE();
}

void CallPrinter::VisitGetTemplateObject(GetTemplateObject* node) {}

void CallPrinter::VisitTemplateLiteral(TemplateLiteral* node) {
  for (Expression* substitution : *node->substitutions()) {
    Find(substitution, true);
  }
}

void CallPrinter::VisitImportCallExpression(ImportCallExpression* node) {
  Print("import");
  if (node->phase() == ModuleImportPhase::kSource) {
    Print(".source");
  }
  Print("(");
  Find(node->specifier(), true);
  if (node->import_options()) {
    Print(", ");
    Find(node->import_options(), true);
  }
  Print(")");
}

void CallPrinter::VisitThisExpression(ThisExpression* node) { Print("this"); }

void CallPrinter::VisitSuperPropertyReference(SuperPropertyReference* node) {}


void CallPrinter::VisitSuperCallReference(SuperCallReference* node) {
  Print("super");
}


void CallPrinter::FindStatements(const ZonePtrList<Statement>* statements) {
  if (statements == nullptr) return;
  for (int i = 0; i < statements->length(); i++) {
    Find(statements->at(i));
  }
}

void CallPrinter::FindArguments(const ZonePtrList<Expression>* arguments) {
  if (found_) return;
  for (int i = 0; i < arguments->length(); i++) {
    Find(arguments->at(i));
  }
}

void CallPrinter::PrintLiteral(DirectHandle<Object> value, bool quote) {
  if (IsString(*value)) {
    if (quote) Print("\"");
    Print(Cast<String>(value));
    if (quote) Print("\"");
  } else if (IsNull(*value, isolate_)) {
    Print("null");
  } else if (IsTrue(*value, isolate_)) {
    Print("true");
  } else if (IsFalse(*value, isolate_)) {
    Print("false");
  } else if (IsUndefined(*value, isolate_)) {
    Print("undefined");
  } else if (IsNumber(*value)) {
    Print(isolate_->factory()->NumberToString(value));
  } else if (IsSymbol(*value)) {
    // Symbols can only occur as literals if they were inserted by the parser.
    PrintLiteral(direct_handle(Cast<Symbol>(value)->description(), isolate_),
                 false);
  }
}

void CallPrinter::PrintLiteral(const AstRawString* value, bool quote) {
  PrintLiteral(value->string(), quote);
}

//-----------------------------------------------------------------------------


#ifdef DEBUG

const char* AstPrinter::Print(AstNode* node) {
  Init();
  Visit(node);
  return output_;
}

void AstPrinter::Init() {
  if (size_ == 0) {
    DCHECK_NULL(output_);
    const int initial_size = 256;
    output_ = NewArray<char>(initial_size);
    size_ = initial_size;
  }
  output_[0] = '\0';
  pos_ = 0;
}

void AstPrinter::Print(const char* format, ...) {
  for (;;) {
    va_list arguments;
    va_start(arguments, format);
    int n = base::VSNPrintF(base::Vector<char>(output_, size_) + pos_, format,
                            arguments);
    va_end(arguments);

    if (n >= 0) {
      // there was enough space - we are done
      pos_ += n;
      return;
    } else {
      // there was not enough space - allocate more and try again
      const int slack = 32;
      int new_size = size_ + (size_ >> 1) + slack;
      char* new_output = NewArray<char>(new_size);
      MemCopy(new_output, output_, pos_);
      DeleteArray(output_);
      output_ = new_output;
      size_ = new_size;
    }
  }
}

void AstPrinter::PrintLiteral(Literal* literal, bool quote) {
  switch (literal->type()) {
    case Literal::kString:
      PrintLiteral(literal->AsRawString(), quote);
      break;
    case Literal::kConsString:
      PrintLiteral(literal->AsConsString(), quote);
      break;
    case Literal::kSmi:
      Print("%d", Smi::ToInt(literal->AsSmiLiteral()));
      break;
    case Literal::kHeapNumber:
      Print("%g", literal->AsNumber());
      break;
    case Literal::kBigInt:
      Print("%sn", literal->AsBigInt().c_str());
      break;
    case Literal::kNull:
      Print("null");
      break;
    case Literal::kUndefined:
      Print("undefined");
      break;
    case Literal::kTheHole:
      Print("the hole");
      break;
    case Literal::kBoolean:
      if (literal->ToBooleanIsTrue()) {
        Print("true");
      } else {
        Print("false");
      }
      break;
  }
}

void AstPrinter::PrintLiteral(const AstRawString* value, bool quote) {
  if (quote) Print("\"");
  if (value != nullptr) {
    const char* format = value->is_one_byte() ? "%c" : "%lc";
    const int increment = value->is_one_byte() ? 1 : 2;
    const unsigned char* raw_bytes = value->raw_data();
    for (int i = 0; i < value->length(); i += increment) {
      Print(format, raw_bytes[i]);
    }
  }
  if (quote) Print("\"");
}

void AstPrinter::PrintLiteral(const AstConsString* value, bool quote) {
  if (quote) Print("\"");
  if (value != nullptr) {
    std::forward_list<const AstRawString*> strings = value->ToRawStrings();
    for (const AstRawString* string : strings) {
      PrintLiteral(string, false);
    }
  }
  if (quote) Print("\"");
}

//-----------------------------------------------------------------------------

class V8_NODISCARD IndentedScope {
 public:
  IndentedScope(AstPrinter* printer, const char* txt)
      : ast_printer_(printer) {
    ast_printer_->PrintIndented(txt);
    ast_printer_->Print("\n");
    ast_printer_->inc_indent();
  }

  IndentedScope(AstPrinter* printer, const char* txt, int pos)
      : ast_printer_(printer) {
    ast_printer_->PrintIndented(txt);
    ast_printer_->Print(" at %d\n", pos);
    ast_printer_->inc_indent();
  }

  virtual ~IndentedScope() {
    ast_printer_->dec_indent();
  }

 private:
  AstPrinter* ast_printer_;
};

//-----------------------------------------------------------------------------

AstPrinter::AstPrinter(uintptr_t stack_limit)
    : output_(nullptr), size_(0), pos_(0), indent_(0) {
  InitializeAstVisitor(stack_limit);
}

AstPrinter::~AstPrinter() {
  DCHECK_EQ(indent_, 0);
  DeleteArray(output_);
}


void AstPrinter::PrintIndented(const char* txt) {
  for (int i = 0; i < indent_; i++) {
    Print(". ");
  }
  Print("%s", txt);
}

void AstPrinter::PrintLiteralIndented(const char* info, Literal* literal,
                                      bool quote) {
  PrintIndented(info);
  Print(" ");
  PrintLiteral(literal, quote);
  Print("\n");
}

void AstPrinter::PrintLiteralIndented(const char* info,
                                      const AstRawString* value, bool quote) {
  PrintIndented(info);
  Print(" ");
  PrintLiteral(value, quote);
  Print("\n");
}

void AstPrinter::PrintLiteralIndented(const char* info,
                                      const AstConsString* value, bool quote) {
  PrintIndented(info);
  Print(" ");
  PrintLiteral(value, quote);
  Print("\n");
}

void AstPrinter::PrintLiteralWithModeIndented(const char* info, Variable* var,
                                              const AstRawString* value) {
  if (var == nullptr) {
    PrintLiteralIndented(info, value, true);
  } else {
    base::EmbeddedVector<char, 256> buf;
    int pos =
        SNPrintF(buf, "%s (%p) (mode = %s, assigned = %s", info,
                 reinterpret_cast<void*>(var), VariableMode2String(var->mode()),
                 var->maybe_assigned() == kMaybeAssigned ? "true" : "false");
    SNPrintF(buf + pos, ")");
    PrintLiteralIndented(buf.begin(), value, true);
  }
}

void AstPrinter::PrintIndentedVisit(const char* s, AstNode* node) {
  if (node != nullptr) {
    IndentedScope indent(this, s, node->position());
    Visit(node);
  }
}


const char* AstPrinter::PrintProgram(FunctionLiteral* program) {
  Init();
  { IndentedScope indent(this, "FUNC", program->position());
    PrintIndented("KIND");
    Print(" %d\n", static_cast<uint32_t>(program->kind()));
    PrintIndented("LITERAL ID");
    Print(" %d\n", program->function_literal_id());
    PrintIndented("SUSPEND COUNT");
    Print(" %d\n", program->suspend_count());
    PrintLiteralIndented("NAME", program->raw_name(), true);
    if (program->raw_inferred_name()) {
      PrintLiteralIndented("INFERRED NAME", program->raw_inferred_name(), true);
    }
    if (program->requires_instance_members_initializer()) {
      Print(" REQUIRES INSTANCE FIELDS INITIALIZER\n");
    }
    if (program->class_scope_has_private_brand()) {
      Print(" CLASS SCOPE HAS PRIVATE BRAND\n");
    }
    if (program->has_static_private_methods_or_accessors()) {
      Print(" HAS STATIC PRIVATE METHODS\n");
    }
    PrintParameters(program->scope());
    PrintDeclarations(program->scope()->declarations());
    PrintStatements(program->body());
  }
  return output_;
}


void AstPrinter::PrintOut(Isolate* isolate, AstNode* node) {
  AstPrinter printer(isolate ? isolate->stack_guard()->real_climit() : 0);
  printer.Init();
  printer.Visit(node);
  PrintF("%s", printer.output_);
}

void AstPrinter::PrintDeclarations(Declaration::List* declarations) {
  if (!declarations->is_empty()) {
    IndentedScope indent(this, "DECLS");
    for (Declaration* decl : *declarations) Visit(decl);
  }
}

void AstPrinter::PrintParameters(DeclarationScope* scope) {
  if (scope->num_parameters() > 0) {
    IndentedScope indent(this, "PARAMS");
    for (int i = 0; i < scope->num_parameters(); i++) {
      PrintLiteralWithModeIndented("VAR", scope->parameter(i),
                                   scope->parameter(i)->raw_name());
    }
  }
}

void AstPrinter::PrintStatements(const ZonePtrList<Statement>* statements) {
  for (int i = 0; i < statements->length(); i++) {
    Visit(statements->at(i));
  }
}

void AstPrinter::PrintArguments(const ZonePtrList<Expression>* arguments) {
  for (int i = 0; i < arguments->length(); i++) {
    Visit(arguments->at(i));
  }
}


void AstPrinter::VisitBlock(Block* node) {
  const char* block_txt =
      node->ignore_completion_value() ? "BLOCK NOCOMPLETIONS" : "BLOCK";
  IndentedScope indent(this, block_txt, node->position());
  PrintStatements(node->statements());
}


// TODO(svenpanne) Start with IndentedScope.
void AstPrinter::VisitVariableDeclaration(VariableDeclaration* node) {
  PrintLiteralWithModeIndented("VARIABLE", node->var(),
                               node->var()->raw_name());
}


// TODO(svenpanne) Start with IndentedScope.
void AstPrinter::VisitFunctionDeclaration(FunctionDeclaration* node) {
  PrintIndented("FUNCTION ");
  PrintLiteral(node->var()->raw_name(), true);
  Print(" = function ");
  PrintLiteral(node->fun()->raw_name(), false);
  Print("\n");
}


void AstPrinter::VisitExpressionStatement(ExpressionStatement* node) {
  IndentedScope indent(this, "EXPRESSION STATEMENT", node->position());
  Visit(node->expression());
}


void AstPrinter::VisitEmptyStatement(EmptyStatement* node) {
  IndentedScope indent(this, "EMPTY", node->position());
}


void AstPrinter::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* node) {
  Visit(node->statement());
}


void AstPrinter::VisitIfStatement(IfStatement* node) {
  IndentedScope indent(this, "IF", node->position());
  PrintIndentedVisit("CONDITION", node->condition());
  PrintIndentedVisit("THEN", node->then_statement());
  if (node->HasElseStatement()) {
    PrintIndentedVisit("ELSE", node->else_statement());
  }
}


void AstPrinter::VisitContinueStatement(ContinueStatement* node) {
  IndentedScope indent(this, "CONTINUE", node->position());
}


void AstPrinter::VisitBreakStatement(BreakStatement* node) {
  IndentedScope indent(this, "BREAK", node->position());
}


void AstPrinter::VisitReturnStatement(ReturnStatement* node) {
  IndentedScope indent(this, "RETURN", node->position());
  Visit(node->expression());
}


void AstPrinter::VisitWithStatement(WithStatement* node) {
  IndentedScope indent(this, "WITH", node->position());
  PrintIndentedVisit("OBJECT", node->expression());
  PrintIndentedVisit("BODY", node->statement());
}


void AstPrinter::VisitSwitchStatement(SwitchStatement* node) {
  IndentedScope switch_indent(this, "SWITCH", node->position());
  PrintIndentedVisit("TAG", node->tag());
  for (CaseClause* clause : *node->cases()) {
    if (clause->is_default()) {
      IndentedScope indent(this, "DEFAULT");
      PrintStatements(clause->statements());
    } else {
      IndentedScope indent(this, "CASE");
      Visit(clause->label());
      PrintStatements(clause->statements());
    }
  }
}


void AstPrinter::VisitDoWhileStatement(DoWhileStatement* node) {
  IndentedScope indent(this, "DO", node->position());
  PrintIndentedVisit("BODY", node->body());
  PrintIndentedVisit("COND", node->cond());
}


void AstPrinter::VisitWhileStatement(WhileStatement* node) {
  IndentedScope indent(this, "WHILE", node->position());
  PrintIndentedVisit("COND", node->cond());
  PrintIndentedVisit("BODY", node->body());
}


void AstPrinter::VisitForStatement(ForStatement* node) {
  IndentedScope indent(this, "FOR", node->position());
  if (node->init()) PrintIndentedVisit("INIT", node->init());
  if (node->cond()) PrintIndentedVisit("COND", node->cond());
  PrintIndentedVisit("BODY", node->body());
  if (node->next()) PrintIndentedVisit("NEXT", node->next());
}


void AstPrinter::VisitForInStatement(ForInStatement* node) {
  IndentedScope indent(this, "FOR IN", node->position());
  PrintIndentedVisit("FOR", node->each());
  PrintIndentedVisit("IN", node->subject());
  PrintIndentedVisit("BODY", node->body());
}


void AstPrinter::VisitForOfStatement(ForOfStatement* node) {
  IndentedScope indent(this, "FOR OF", node->position());
  const char* for_type;
  switch (node->type()) {
    case IteratorType::kNormal:
      for_type = "FOR";
      break;
    case IteratorType::kAsync:
      for_type = "FOR AWAIT";
      break;
  }
  PrintIndentedVisit(for_type, node->each());
  PrintIndentedVisit("OF", node->subject());
  PrintIndentedVisit("BODY", node->body());
}


void AstPrinter::VisitTryCatchStatement(TryCatchStatement* node) {
  IndentedScope indent(this, "TRY CATCH", node->position());
  PrintIndentedVisit("TRY", node->try_block());
  PrintIndented("CATCH PREDICTION");
  const char* prediction = "";
  switch (node->GetCatchPrediction(HandlerTable::UNCAUGHT)) {
    case HandlerTable::UNCAUGHT:
      prediction = "UNCAUGHT";
      break;
    case HandlerTable::CAUGHT:
      prediction = "CAUGHT";
      break;
    case HandlerTable::ASYNC_AWAIT:
      prediction = "ASYNC_AWAIT";
      break;
    case HandlerTable::UNCAUGHT_ASYNC_AWAIT:
      prediction = "UNCAUGHT_ASYNC_AWAIT";
      break;
    case HandlerTable::PROMISE:
      // Catch prediction resulting in promise rejections aren't
      // parsed by the parser.
      UNREACHABLE();
  }
  Print(" %s\n", prediction);
  if (node->scope()) {
    PrintLiteralWithModeIndented("CATCHVAR", node->scope()->catch_variable(),
                                 node->scope()->catch_variable()->raw_name());
  }
  PrintIndentedVisit("CATCH", node->catch_block());
}

void AstPrinter::VisitTryFinallyStatement(TryFinallyStatement* node) {
  IndentedScope indent(this, "TRY FINALLY", node->position());
  PrintIndentedVisit("TRY", node->try_block());
  PrintIndentedVisit("FINALLY", node->finally_block());
}

void AstPrinter::VisitDebuggerStatement(DebuggerStatement* node) {
  IndentedScope indent(this, "DEBUGGER", node->position());
}


void AstPrinter::VisitFunctionLiteral(FunctionLiteral* node) {
  IndentedScope indent(this, "FUNC LITERAL", node->position());
  PrintIndented("LITERAL ID");
  Print(" %d\n", node->function_literal_id());
  PrintLiteralIndented("NAME", node->raw_name(), false);
  PrintLiteralIndented("INFERRED NAME", node->raw_inferred_name(), false);
  // We don't want to see the function literal in this case: it
  // will be printed via PrintProgram when the code for it is
  // generated.
  // PrintParameters(node->scope());
  // PrintStatements(node->body());
}


void AstPrinter::VisitClassLiteral(ClassLiteral* node) {
  IndentedScope indent(this, "CLASS LITERAL", node->position());
  PrintLiteralIndented("NAME", node->constructor()->raw_name(), false);
  if (node->extends() != nullptr) {
    PrintIndentedVisit("EXTENDS", node->extends());
  }
  Scope* outer = node->constructor()->scope()->outer_scope();
  if (outer->is_class_scope()) {
    Variable* brand = outer->AsClassScope()->brand();
    if (brand != nullptr) {
      PrintLiteralWithModeIndented("BRAND", brand, brand->raw_name());
    }
  }
  if (node->static_initializer() != nullptr) {
    PrintIndentedVisit("STATIC INITIALIZER", node->static_initializer());
  }
  if (node->instance_members_initializer_function() != nullptr) {
    PrintIndentedVisit("INSTANCE MEMBERS INITIALIZER",
                       node->instance_members_initializer_function());
  }
  PrintClassProperties(node->private_members());
  PrintClassProperties(node->public_members());
}

void AstPrinter::VisitInitializeClassMembersStatement(
    InitializeClassMembersStatement* node) {
  IndentedScope indent(this, "INITIALIZE CLASS MEMBERS", node->position());
  PrintClassProperties(node->fields());
}

void AstPrinter::VisitInitializeClassStaticElementsStatement(
    InitializeClassStaticElementsStatement* node) {
  IndentedScope indent(this, "INITIALIZE CLASS STATIC ELEMENTS",
                       node->position());
  PrintClassStaticElements(node->elements());
}

void AstPrinter::VisitAutoAccessorGetterBody(AutoAccessorGetterBody* node) {
  IndentedScope indent(this, "AUTO ACCESSOR GETTER BODY", node->position());
  PrintIndentedVisit("AUTO ACCESSOR STORAGE PRIVATE NAME", node->name_proxy());
}

void AstPrinter::VisitAutoAccessorSetterBody(AutoAccessorSetterBody* node) {
  IndentedScope indent(this, "AUTO ACCESSOR SETTER BODY", node->position());
  PrintIndentedVisit("AUTO ACCESSOR STORAGE PRIVATE NAME", node->name_proxy());
}

void AstPrinter::PrintClassProperty(ClassLiteral::Property* property) {
  const char* prop_kind = nullptr;
  switch (property->kind()) {
    case ClassLiteral::Property::METHOD:
      prop_kind = "METHOD";
      break;
    case ClassLiteral::Property::GETTER:
      prop_kind = "GETTER";
      break;
    case ClassLiteral::Property::SETTER:
      prop_kind = "SETTER";
      break;
    case ClassLiteral::Property::FIELD:
      prop_kind = "FIELD";
      break;
    case ClassLiteral::Property::AUTO_ACCESSOR:
      prop_kind = "AUTO ACCESSOR";
      break;
  }
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "PROPERTY%s%s - %s", property->is_static() ? " - STATIC" : "",
           property->is_private() ? " - PRIVATE" : " - PUBLIC", prop_kind);
  IndentedScope prop(this, buf.begin());
  PrintIndentedVisit("KEY", property->key());
  PrintIndentedVisit("VALUE", property->value());
}

void AstPrinter::PrintClassProperties(
    const ZonePtrList<ClassLiteral::Property>* properties) {
  for (int i = 0; i < properties->length(); i++) {
    PrintClassProperty(properties->at(i));
  }
}

void AstPrinter::PrintClassStaticElements(
    const ZonePtrList<ClassLiteral::StaticElement>* static_elements) {
  for (int i = 0; i < static_elements->length(); i++) {
    ClassLiteral::StaticElement* element = static_elements->at(i);
    switch (element->kind()) {
      case ClassLiteral::StaticElement::PROPERTY:
        PrintClassProperty(element->property());
        break;
      case ClassLiteral::StaticElement::STATIC_BLOCK:
        PrintIndentedVisit("STATIC BLOCK", element->static_block());
        break;
    }
  }
}

void AstPrinter::VisitNativeFunctionLiteral(NativeFunctionLiteral* node) {
  IndentedScope indent(this, "NATIVE FUNC LITERAL", node->position());
  PrintLiteralIndented("NAME", node->raw_name(), false);
}

void AstPrinter::VisitConditionalChain(ConditionalChain* node) {
  IndentedScope indent(this, "CONDITIONAL_CHAIN", node->position());
  PrintIndentedVisit("CONDITION", node->condition_at(0));
  PrintIndentedVisit("THEN", node->then_expression_at(0));
  for (size_t i = 1; i < node->conditional_chain_length(); ++i) {
    IndentedScope inner_indent(this, "ELSE IF", node->condition_position_at(i));
    PrintIndentedVisit("CONDITION", node->condition_at(i));
    PrintIndentedVisit("THEN", node->then_expression_at(i));
  }
  PrintIndentedVisit("ELSE", node->else_expression());
}

void AstPrinter::VisitConditional(Conditional* node) {
  IndentedScope indent(this, "CONDITIONAL", node->position());
  PrintIndentedVisit("CONDITION", node->condition());
  PrintIndentedVisit("THEN", node->then_expression());
  PrintIndentedVisit("ELSE", node->else_expression());
}


void AstPrinter::VisitLiteral(Literal* node) {
  PrintLiteralIndented("LITERAL", node, true);
}


void AstPrinter::VisitRegExpLiteral(RegExpLiteral* node) {
  IndentedScope indent(this, "REGEXP LITERAL", node->position());
  PrintLiteralIndented("PATTERN", node->raw_pattern(), false);
  int i = 0;
  base::EmbeddedVector<char, 128> buf;
#define V(Lower, Camel, LowerCamel, Char, Bit) \
  if (node->flags() & RegExp::k##Camel) buf[i++] = Char;
  REGEXP_FLAG_LIST(V)
#undef V
  buf[i] = '\0';
  PrintIndented("FLAGS ");
  Print("%s", buf.begin());
  Print("\n");
}


void AstPrinter::VisitObjectLiteral(ObjectLiteral* node) {
  IndentedScope indent(this, "OBJ LITERAL", node->position());
  PrintObjectProperties(node->properties());
}

void AstPrinter::PrintObjectProperties(
    const ZonePtrList<ObjectLiteral::Property>* properties) {
  for (int i = 0; i < properties->length(); i++) {
    ObjectLiteral::Property* property = properties->at(i);
    const char* prop_kind = nullptr;
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
        prop_kind = "CONSTANT";
        break;
      case ObjectLiteral::Property::COMPUTED:
        prop_kind = "COMPUTED";
        break;
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        prop_kind = "MATERIALIZED_LITERAL";
        break;
      case ObjectLiteral::Property::PROTOTYPE:
        prop_kind = "PROTOTYPE";
        break;
      case ObjectLiteral::Property::GETTER:
        prop_kind = "GETTER";
        break;
      case ObjectLiteral::Property::SETTER:
        prop_kind = "SETTER";
        break;
      case ObjectLiteral::Property::SPREAD:
        prop_kind = "SPREAD";
        break;
    }
    base::EmbeddedVector<char, 128> buf;
    SNPrintF(buf, "PROPERTY - %s", prop_kind);
    IndentedScope prop(this, buf.begin());
    PrintIndentedVisit("KEY", properties->at(i)->key());
    PrintIndentedVisit("VALUE", properties->at(i)->value());
  }
}


void AstPrinter::VisitArrayLiteral(ArrayLiteral* node) {
  IndentedScope array_indent(this, "ARRAY LITERAL", node->position());
  if (node->values()->length() > 0) {
    IndentedScope indent(this, "VALUES", node->position());
    for (int i = 0; i < node->values()->length(); i++) {
      Visit(node->values()->at(i));
    }
  }
}


void AstPrinter::VisitVariableProxy(VariableProxy* node) {
  base::EmbeddedVector<char, 128> buf;
  int pos = SNPrintF(buf, "VAR PROXY");

  if (!node->is_resolved()) {
    SNPrintF(buf + pos, " unresolved");
    PrintLiteralWithModeIndented(buf.begin(), nullptr, node->raw_name());
  } else {
    Variable* var = node->var();
    switch (var->location()) {
      case VariableLocation::UNALLOCATED:
        SNPrintF(buf + pos, " unallocated");
        break;
      case VariableLocation::PARAMETER:
        SNPrintF(buf + pos, " parameter[%d]", var->index());
        break;
      case VariableLocation::LOCAL:
        SNPrintF(buf + pos, " local[%d]", var->index());
        break;
      case VariableLocation::CONTEXT:
        SNPrintF(buf + pos, " context[%d]", var->index());
        break;
      case VariableLocation::LOOKUP:
        SNPrintF(buf + pos, " lookup");
        break;
      case VariableLocation::MODULE:
        SNPrintF(buf + pos, " module");
        break;
      case VariableLocation::REPL_GLOBAL:
        SNPrintF(buf + pos, " repl global[%d]", var->index());
        break;
    }
    PrintLiteralWithModeIndented(buf.begin(), var, node->raw_name());
  }
}


void AstPrinter::VisitAssignment(Assignment* node) {
  IndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->target());
  Visit(node->value());
}

void AstPrinter::VisitCompoundAssignment(CompoundAssignment* node) {
  VisitAssignment(node);
}

void AstPrinter::VisitYield(Yield* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "YIELD");
  IndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}

void AstPrinter::VisitYieldStar(YieldStar* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "YIELD_STAR");
  IndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}

void AstPrinter::VisitAwait(Await* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "AWAIT");
  IndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}

void AstPrinter::VisitThrow(Throw* node) {
  IndentedScope indent(this, "THROW", node->position());
  Visit(node->exception());
}

void AstPrinter::VisitOptionalChain(OptionalChain* node) {
  IndentedScope indent(this, "OPTIONAL_CHAIN", node->position());
  Visit(node->expression());
}

void AstPrinter::VisitProperty(Property* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "PROPERTY");
  IndentedScope indent(this, buf.begin(), node->position());

  Visit(node->obj());
  AssignType type = Property::GetAssignType(node);
  switch (type) {
    case NAMED_PROPERTY:
    case NAMED_SUPER_PROPERTY: {
      PrintLiteralIndented("NAME", node->key()->AsLiteral(), false);
      break;
    }
    case PRIVATE_METHOD: {
      PrintIndentedVisit("PRIVATE_METHOD", node->key());
      break;
    }
    case PRIVATE_GETTER_ONLY: {
      PrintIndentedVisit("PRIVATE_GETTER_ONLY", node->key());
      break;
    }
    case PRIVATE_SETTER_ONLY: {
      PrintIndentedVisit("PRIVATE_SETTER_ONLY", node->key());
      break;
    }
    case PRIVATE_GETTER_AND_SETTER: {
      PrintIndentedVisit("PRIVATE_GETTER_AND_SETTER", node->key());
      break;
    }
    case KEYED_PROPERTY:
    case KEYED_SUPER_PROPERTY: {
      PrintIndentedVisit("KEY", node->key());
      break;
    }
    case PRIVATE_DEBUG_DYNAMIC: {
      PrintIndentedVisit("PRIVATE_DEBUG_DYNAMIC", node->key());
      break;
    }
    case NON_PROPERTY:
      UNREACHABLE();
  }
}

void AstPrinter::VisitCall(Call* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "CALL");
  IndentedScope indent(this, buf.begin());

  Visit(node->expression());
  PrintArguments(node->arguments());
}


void AstPrinter::VisitCallNew(CallNew* node) {
  IndentedScope indent(this, "CALL NEW", node->position());
  Visit(node->expression());
  PrintArguments(node->arguments());
}


void AstPrinter::VisitCallRuntime(CallRuntime* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "CALL RUNTIME %s", node->function()->name);
  IndentedScope indent(this, buf.begin(), node->position());
  PrintArguments(node->arguments());
}


void AstPrinter::VisitUnaryOperation(UnaryOperation* node) {
  IndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->expression());
}


void AstPrinter::VisitCountOperation(CountOperation* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "%s %s", (node->is_prefix() ? "PRE" : "POST"),
           Token::Name(node->op()));
  IndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}


void AstPrinter::VisitBinaryOperation(BinaryOperation* node) {
  IndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->left());
  Visit(node->right());
}

void AstPrinter::VisitNaryOperation(NaryOperation* node) {
  IndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->first());
  for (size_t i = 0; i < node->subsequent_length(); ++i) {
    Visit(node->subsequent(i));
  }
}

void AstPrinter::VisitCompareOperation(CompareOperation* node) {
  IndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->left());
  Visit(node->right());
}


void AstPrinter::VisitSpread(Spread* node) {
  IndentedScope indent(this, "SPREAD", node->position());
  Visit(node->expression());
}

void AstPrinter::VisitEmptyParentheses(EmptyParentheses* node) {
  IndentedScope indent(this, "()", node->position());
}

void AstPrinter::VisitGetTemplateObject(GetTemplateObject* node) {
  IndentedScope indent(this, "GET-TEMPLATE-OBJECT", node->position());
}

void AstPrinter::VisitTemplateLiteral(TemplateLiteral* node) {
  IndentedScope indent(this, "TEMPLATE-LITERAL", node->position());
  const AstRawString* string = node->string_parts()->first();
  if (!string->IsEmpty()) PrintLiteralIndented("SPAN", string, true);
  for (int i = 0; i < node->substitutions()->length();) {
    PrintIndentedVisit("EXPR", node->substitutions()->at(i++));
    if (i < node->string_parts()->length()) {
      string = node->string_parts()->at(i);
      if (!string->IsEmpty()) PrintLiteralIndented("SPAN", string, true);
    }
  }
}

void AstPrinter::VisitImportCallExpression(ImportCallExpression* node) {
  IndentedScope indent(this, "IMPORT-CALL", node->position());
  PrintIndented("PHASE");
  Print(" %d\n", static_cast<uint32_t>(node->phase()));
  Visit(node->specifier());
  if (node->import_options()) {
    Visit(node->import_options());
  }
}

void AstPrinter::VisitThisExpression(ThisExpression* node) {
  IndentedScope indent(this, "THIS-EXPRESSION", node->position());
}

void AstPrinter::VisitSuperPropertyReference(SuperPropertyReference* node) {
  IndentedScope indent(this, "SUPER-PROPERTY-REFERENCE", node->position());
}

void AstPrinter::VisitSuperCallReference(SuperCallReference* node) {
  IndentedScope indent(this, "SUPER-CALL-REFERENCE", node->position());
}

void AstPrinter::VisitSuperCallForwardArgs(SuperCallForwardArgs* node) {
  IndentedScope indent(this, "SUPER FORWARD-VARARGS", node->position());
  Visit(node->expression());
}

#endif  // DEBUG

}  // namespace internal
}  // namespace v8

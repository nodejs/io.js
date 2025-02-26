// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PARSER_H_
#define V8_PARSING_PARSER_H_

#include <cstddef>

#include "src/ast/ast-source-ranges.h"
#include "src/ast/ast-value-factory.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/base/compiler-specific.h"
#include "src/base/pointer-with-payload.h"
#include "src/base/small-vector.h"
#include "src/base/threaded-list.h"
#include "src/common/globals.h"
#include "src/parsing/import-attributes.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser-base.h"
#include "src/parsing/parsing.h"
#include "src/parsing/preparser.h"
#include "src/zone/zone-chunk-list.h"

namespace v8 {

class ScriptCompiler;

namespace internal {

class ConsumedPreparseData;
class ParseInfo;
class ParserTarget;
class ParserTargetScope;
class PendingCompilationErrorHandler;
class PreparseData;

// ----------------------------------------------------------------------------
// JAVASCRIPT PARSING

class Parser;


struct ParserFormalParameters : FormalParametersBase {
  struct Parameter : public ZoneObject {
    Parameter(Expression* pattern, Expression* initializer, int position,
              int initializer_end_position, bool is_rest)
        : initializer_and_is_rest(initializer, is_rest),
          pattern(pattern),
          position(position),
          initializer_end_position(initializer_end_position) {}

    base::PointerWithPayload<Expression, bool, 1> initializer_and_is_rest;

    Expression* pattern;
    Expression* initializer() const {
      return initializer_and_is_rest.GetPointer();
    }
    int position;
    int initializer_end_position;
    inline bool is_rest() const { return initializer_and_is_rest.GetPayload(); }

    Parameter* next_parameter = nullptr;
    bool is_simple() const {
      return pattern->IsVariableProxy() && initializer() == nullptr &&
             !is_rest();
    }

    const AstRawString* name() const {
      DCHECK(is_simple());
      return pattern->AsVariableProxy()->raw_name();
    }

    Parameter** next() { return &next_parameter; }
    Parameter* const* next() const { return &next_parameter; }
  };

  void set_strict_parameter_error(const Scanner::Location& loc,
                                  MessageTemplate message) {
    strict_error_loc = loc;
    strict_error_message = message;
  }

  bool has_duplicate() const { return duplicate_loc.IsValid(); }
  void ValidateDuplicate(Parser* parser) const;
  void ValidateStrictMode(Parser* parser) const;

  explicit ParserFormalParameters(DeclarationScope* scope)
      : FormalParametersBase(scope) {}

  base::ThreadedList<Parameter> params;
  Scanner::Location duplicate_loc = Scanner::Location::invalid();
  Scanner::Location strict_error_loc = Scanner::Location::invalid();
  MessageTemplate strict_error_message = MessageTemplate::kNone;
};

template <>
struct ParserTypes<Parser> {
  using Base = ParserBase<Parser>;
  using Impl = Parser;

  // Return types for traversing functions.
  using Block = v8::internal::Block*;
  using BreakableStatement = v8::internal::BreakableStatement*;
  using ClassLiteralProperty = ClassLiteral::Property*;
  using ClassLiteralStaticElement = ClassLiteral::StaticElement*;
  using ClassPropertyList = ZonePtrList<ClassLiteral::Property>*;
  using ClassStaticElementList = ZonePtrList<ClassLiteral::StaticElement>*;
  using Expression = v8::internal::Expression*;
  using ExpressionList = ScopedPtrList<v8::internal::Expression>;
  using FormalParameters = ParserFormalParameters;
  using ForStatement = v8::internal::ForStatement*;
  using FunctionLiteral = v8::internal::FunctionLiteral*;
  using Identifier = const AstRawString*;
  using IterationStatement = v8::internal::IterationStatement*;
  using ObjectLiteralProperty = ObjectLiteral::Property*;
  using ObjectPropertyList = ScopedPtrList<v8::internal::ObjectLiteralProperty>;
  using Statement = v8::internal::Statement*;
  using StatementList = ScopedPtrList<v8::internal::Statement>;
  using Suspend = v8::internal::Suspend*;

  // For constructing objects returned by the traversing functions.
  using Factory = AstNodeFactory;

  // Other implementation-specific functions.
  using FuncNameInferrer = v8::internal::FuncNameInferrer;
  using SourceRange = v8::internal::SourceRange;
  using SourceRangeScope = v8::internal::SourceRangeScope;
};

class V8_EXPORT_PRIVATE Parser : public NON_EXPORTED_BASE(ParserBase<Parser>) {
 public:
  Parser(LocalIsolate* local_isolate, ParseInfo* info);
  ~Parser() {
    delete reusable_preparser_;
    reusable_preparser_ = nullptr;
  }

  static bool IsPreParser() { return false; }

  // Sets the literal on |info| if parsing succeeded.
  void ParseOnBackground(LocalIsolate* isolate, ParseInfo* info,
                         DirectHandle<Script> script, int start_position,
                         int end_position, int function_literal_id);

  // Initializes an empty scope chain for top-level scripts, or scopes which
  // consist of only the native context.
  void InitializeEmptyScopeChain(ParseInfo* info);

  // Deserialize the scope chain prior to parsing in which the script is going
  // to be executed. If the script is a top-level script, or the scope chain
  // consists of only a native context, maybe_outer_scope_info should be an
  // empty handle.
  //
  // This only deserializes the scope chain, but doesn't connect the scopes to
  // their corresponding scope infos. Therefore, looking up variables in the
  // deserialized scopes is not possible.
  template <typename IsolateT>
  void DeserializeScopeChain(
      IsolateT* isolate, ParseInfo* info,
      MaybeDirectHandle<ScopeInfo> maybe_outer_scope_info,
      Scope::DeserializationMode mode =
          Scope::DeserializationMode::kScopesOnly);

  // Move statistics to Isolate
  void UpdateStatistics(Isolate* isolate, DirectHandle<Script> script);
  void UpdateStatistics(
      DirectHandle<Script> script,
      base::SmallVector<v8::Isolate::UseCounterFeature, 8>* use_counters,
      int* preparse_skipped);
  template <typename IsolateT>
  void HandleSourceURLComments(IsolateT* isolate, DirectHandle<Script> script);

 private:
  friend class ParserBase<Parser>;
  friend struct ParserFormalParameters;
  friend class i::ExpressionScope<ParserTypes<Parser>>;
  friend class i::VariableDeclarationParsingScope<ParserTypes<Parser>>;
  friend class i::ParameterDeclarationParsingScope<ParserTypes<Parser>>;
  friend class i::ArrowHeadParsingScope<ParserTypes<Parser>>;
  friend bool v8::internal::parsing::ParseProgram(
      ParseInfo*, DirectHandle<Script>,
      MaybeDirectHandle<ScopeInfo> maybe_outer_scope_info, Isolate*,
      parsing::ReportStatisticsMode stats_mode);
  friend bool v8::internal::parsing::ParseFunction(
      ParseInfo*, DirectHandle<SharedFunctionInfo> shared_info, Isolate*,
      parsing::ReportStatisticsMode stats_mode);

  bool AllowsLazyParsingWithoutUnresolvedVariables() const {
    return !MaybeParsingArrowhead() &&
           scope()->AllowsLazyParsingWithoutUnresolvedVariables(
               original_scope_);
  }

  bool parse_lazily() const { return mode_ == PARSE_LAZILY; }
  enum Mode { PARSE_LAZILY, PARSE_EAGERLY };

  class V8_NODISCARD ParsingModeScope {
   public:
    ParsingModeScope(Parser* parser, Mode mode)
        : parser_(parser), old_mode_(parser->mode_) {
      parser_->mode_ = mode;
    }
    ~ParsingModeScope() { parser_->mode_ = old_mode_; }

   private:
    Parser* parser_;
    Mode old_mode_;
  };

  // Runtime encoding of different completion modes.
  enum CompletionKind {
    kNormalCompletion,
    kThrowCompletion,
    kAbruptCompletion
  };

  Variable* NewTemporary(const AstRawString* name) {
    return scope()->NewTemporary(name);
  }

  void PrepareGeneratorVariables();

  // Sets the literal on |info| if parsing succeeded.
  void ParseProgram(Isolate* isolate, DirectHandle<Script> script,
                    ParseInfo* info,
                    MaybeDirectHandle<ScopeInfo> maybe_outer_scope_info);

  // Sets the literal on |info| if parsing succeeded.
  void ParseFunction(Isolate* isolate, ParseInfo* info,
                     DirectHandle<SharedFunctionInfo> shared_info);

  template <typename IsolateT>
  void PostProcessParseResult(IsolateT* isolate, ParseInfo* info,
                              FunctionLiteral* literal);

  FunctionLiteral* DoParseFunction(Isolate* isolate, ParseInfo* info,
                                   int start_position, int end_position,
                                   int function_literal_id,
                                   const AstRawString* raw_name);

  FunctionLiteral* ParseClassForMemberInitialization(
      FunctionKind initializer_kind, int initializer_pos, int initializer_id,
      int initializer_end_pos, const AstRawString* class_name);

  // Called by ParseProgram after setting up the scanner.
  FunctionLiteral* DoParseProgram(Isolate* isolate, ParseInfo* info);

  // Parse with the script as if the source is implicitly wrapped in a function.
  // We manually construct the AST and scopes for a top-level function and the
  // function wrapper.
  void ParseWrapped(Isolate* isolate, ParseInfo* info,
                    ScopedPtrList<Statement>* body, DeclarationScope* scope,
                    Zone* zone);

  void ParseREPLProgram(ParseInfo* info, ScopedPtrList<Statement>* body,
                        DeclarationScope* scope);
  Expression* WrapREPLResult(Expression* value);

  ZonePtrList<const AstRawString>* PrepareWrappedArguments(Isolate* isolate,
                                                           ParseInfo* info,
                                                           Zone* zone);

  PreParser* reusable_preparser() {
    if (reusable_preparser_ == nullptr) {
      reusable_preparser_ = new PreParser(
          &preparser_zone_, &scanner_, stack_limit_, ast_value_factory(),
          pending_error_handler(), runtime_call_stats_, v8_file_logger_,
          flags(), parsing_on_main_thread_);
      reusable_preparser_->set_allow_eval_cache(allow_eval_cache());
      preparse_data_buffer_.reserve(128);
    }
    return reusable_preparser_;
  }

  void ParseModuleItemList(ScopedPtrList<Statement>* body);
  Statement* ParseModuleItem();
  const AstRawString* ParseModuleSpecifier();
  void ParseImportDeclaration();
  Statement* ParseExportDeclaration();
  Statement* ParseExportDefault();
  void ParseExportStar();
  struct ExportClauseData {
    const AstRawString* export_name;
    const AstRawString* local_name;
    Scanner::Location location;
  };
  ZoneChunkList<ExportClauseData>* ParseExportClause(
      Scanner::Location* reserved_loc,
      Scanner::Location* string_literal_local_name_loc);
  struct NamedImport : public ZoneObject {
    const AstRawString* import_name;
    const AstRawString* local_name;
    const Scanner::Location location;
    NamedImport(const AstRawString* import_name, const AstRawString* local_name,
                Scanner::Location location)
        : import_name(import_name),
          local_name(local_name),
          location(location) {}
  };
  const AstRawString* ParseExportSpecifierName();
  ZonePtrList<const NamedImport>* ParseNamedImports(int pos);

  ImportAttributes* ParseImportWithOrAssertClause();
  Statement* BuildInitializationBlock(DeclarationParsingResult* parsing_result);
  Statement* RewriteSwitchStatement(SwitchStatement* switch_statement,
                                    Scope* scope);
  Block* RewriteCatchPattern(CatchInfo* catch_info);
  void ReportVarRedeclarationIn(const AstRawString* name, Scope* scope);
  Statement* RewriteTryStatement(Block* try_block, Block* catch_block,
                                 const SourceRange& catch_range,
                                 Block* finally_block,
                                 const SourceRange& finally_range,
                                 const CatchInfo& catch_info, int pos);
  void ParseGeneratorFunctionBody(int pos, FunctionKind kind,
                                  ScopedPtrList<Statement>* body);
  void ParseAsyncGeneratorFunctionBody(int pos, FunctionKind kind,
                                       ScopedPtrList<Statement>* body);
  void DeclareFunctionNameVar(const AstRawString* function_name,
                              FunctionSyntaxKind function_syntax_kind,
                              DeclarationScope* function_scope);

  Statement* DeclareFunction(const AstRawString* variable_name,
                             FunctionLiteral* function, VariableMode mode,
                             VariableKind kind, int beg_pos, int end_pos,
                             ZonePtrList<const AstRawString>* names);
  VariableProxy* CreateSyntheticContextVariableProxy(ClassScope* scope,
                                                     ClassInfo* class_info,
                                                     const AstRawString* name,
                                                     bool is_static);
  VariableProxy* CreatePrivateNameVariable(ClassScope* scope, VariableMode mode,
                                           IsStaticFlag is_static_flag,
                                           const AstRawString* name);
  FunctionLiteral* CreateInitializerFunction(const AstRawString* class_name,
                                             DeclarationScope* scope,
                                             int function_literal_id,
                                             Statement* initializer_stmt);

  bool IdentifierEquals(const AstRawString* identifier,
                        const AstRawString* other) {
    return identifier == other;
  }

  Statement* DeclareClass(const AstRawString* variable_name, Expression* value,
                          ZonePtrList<const AstRawString>* names,
                          int class_token_pos, int end_pos);
  void DeclareClassVariable(ClassScope* scope, const AstRawString* name,
                            ClassInfo* class_info, int class_token_pos);
  void DeclareClassBrandVariable(ClassScope* scope, ClassInfo* class_info,
                                 int class_token_pos);
  void AddInstanceFieldOrStaticElement(ClassLiteralProperty* property,
                                       ClassInfo* class_info, bool is_static);
  void DeclarePrivateClassMember(ClassScope* scope,
                                 const AstRawString* property_name,
                                 ClassLiteralProperty* property,
                                 ClassLiteralProperty::Kind kind,
                                 bool is_static, ClassInfo* class_info);
  void DeclarePublicClassMethod(const AstRawString* class_name,
                                ClassLiteralProperty* property,
                                bool is_constructor, ClassInfo* class_info);
  void DeclarePublicClassField(ClassScope* scope,
                               ClassLiteralProperty* property, bool is_static,
                               bool is_computed_name, ClassInfo* class_info);
  void DeclareClassProperty(ClassScope* scope, const AstRawString* class_name,
                            ClassLiteralProperty* property, bool is_constructor,
                            ClassInfo* class_info);
  void DeclareClassField(ClassScope* scope, ClassLiteralProperty* property,
                         const AstRawString* property_name, bool is_static,
                         bool is_computed_name, bool is_private,
                         ClassInfo* class_info);
  void AddClassStaticBlock(Block* block, ClassInfo* class_info);
  FunctionLiteral* CreateStaticElementsInitializer(const AstRawString* name,
                                                   ClassInfo* class_info);
  FunctionLiteral* CreateInstanceMembersInitializer(const AstRawString* name,
                                                    ClassInfo* class_info);
  Expression* RewriteClassLiteral(ClassScope* block_scope,
                                  const AstRawString* name,
                                  ClassInfo* class_info, int pos);
  Statement* DeclareNative(const AstRawString* name, int pos);

  Block* IgnoreCompletion(Statement* statement);

  bool HasCheckedSyntax() {
    return scope()->GetDeclarationScope()->has_checked_syntax();
  }

  void InitializeVariables(
      ScopedPtrList<Statement>* statements, VariableKind kind,
      const DeclarationParsingResult::Declaration* declaration);

  Block* RewriteForVarInLegacy(const ForInfo& for_info);
  void DesugarBindingInForEachStatement(ForInfo* for_info, Block** body_block,
                                        Expression** each_variable);
  Block* CreateForEachStatementTDZ(Block* init_block, const ForInfo& for_info);

  Statement* DesugarLexicalBindingsInForStatement(
      ForStatement* loop, Statement* init, Expression* cond, Statement* next,
      Statement* body, Scope* inner_scope, const ForInfo& for_info);

  FunctionLiteral* ParseFunctionLiteral(
      const AstRawString* name, Scanner::Location function_name_location,
      FunctionNameValidity function_name_validity, FunctionKind kind,
      int function_token_position, FunctionSyntaxKind type,
      LanguageMode language_mode,
      ZonePtrList<const AstRawString>* arguments_for_wrapped_function);

  ObjectLiteral* InitializeObjectLiteral(ObjectLiteral* object_literal) {
    object_literal->CalculateEmitStore(main_zone());
    return object_literal;
  }

  // Insert initializer statements for var-bindings shadowing parameter bindings
  // from a non-simple parameter list.
  void InsertShadowingVarBindingInitializers(Block* block);

  // Implement sloppy block-scoped functions, ES2015 Annex B 3.3
  void InsertSloppyBlockFunctionVarBindings(DeclarationScope* scope);

  void DeclareUnboundVariable(const AstRawString* name, VariableMode mode,
                              InitializationFlag init, int pos);
  V8_WARN_UNUSED_RESULT
  VariableProxy* DeclareBoundVariable(const AstRawString* name,
                                      VariableMode mode, int pos);
  void DeclareAndBindVariable(VariableProxy* proxy, VariableKind kind,
                              VariableMode mode, Scope* declaration_scope,
                              bool* was_added, int initializer_position);
  V8_WARN_UNUSED_RESULT
  Variable* DeclareVariable(const AstRawString* name, VariableKind kind,
                            VariableMode mode, InitializationFlag init,
                            Scope* declaration_scope, bool* was_added,
                            int begin, int end = kNoSourcePosition);
  void Declare(Declaration* declaration, const AstRawString* name,
               VariableKind kind, VariableMode mode, InitializationFlag init,
               Scope* declaration_scope, bool* was_added, int var_begin_pos,
               int var_end_pos = kNoSourcePosition);

  // Factory methods.
  FunctionLiteral* DefaultConstructor(const AstRawString* name, bool call_super,
                                      int pos);

  FunctionLiteral* MakeAutoAccessorGetter(VariableProxy* name_proxy,
                                          const AstRawString* name,
                                          bool is_static, int pos);

  FunctionLiteral* MakeAutoAccessorSetter(VariableProxy* name_proxy,
                                          const AstRawString* name,
                                          bool is_static, int pos);

  AutoAccessorInfo* NewAutoAccessorInfo(ClassScope* scope,
                                        ClassInfo* class_info,
                                        const AstRawString* name,
                                        bool is_static, int pos);
  ClassLiteralProperty* NewClassLiteralPropertyWithAccessorInfo(
      ClassScope* scope, ClassInfo* class_info, const AstRawString* name,
      Expression* key, Expression* value, bool is_static, bool is_computed_name,
      bool is_private, int pos);

  // Skip over a lazy function, either using cached data if we have it, or
  // by parsing the function with PreParser. Consumes the ending }.
  // In case the preparser detects an error it cannot identify, it resets the
  // scanner- and preparser state to the initial one, before PreParsing the
  // function.
  // SkipFunction returns true if it correctly parsed the function, including
  // cases where we detect an error. It returns false, if we needed to stop
  // parsing or could not identify an error correctly, meaning the caller needs
  // to fully reparse. In this case it resets the scanner and preparser state.
  bool SkipFunction(const AstRawString* function_name, FunctionKind kind,
                    FunctionSyntaxKind function_syntax_kind,
                    DeclarationScope* function_scope, int* num_parameters,
                    int* function_length,
                    ProducedPreparseData** produced_preparsed_scope_data);

  Block* BuildParameterInitializationBlock(
      const ParserFormalParameters& parameters);

  void ParseFunction(
      ScopedPtrList<Statement>* body, const AstRawString* function_name,
      int pos, FunctionKind kind, FunctionSyntaxKind function_syntax_kind,
      DeclarationScope* function_scope, int* num_parameters,
      int* function_length, bool* has_duplicate_parameters,
      int* expected_property_count, int* suspend_count,
      ZonePtrList<const AstRawString>* arguments_for_wrapped_function);

  void ThrowPendingError(Isolate* isolate, Handle<Script> script);

  class TemplateLiteral : public ZoneObject {
   public:
    TemplateLiteral(Zone* zone, int pos)
        : cooked_(8, zone), raw_(8, zone), expressions_(8, zone), pos_(pos) {}

    const ZonePtrList<const AstRawString>* cooked() const { return &cooked_; }
    const ZonePtrList<const AstRawString>* raw() const { return &raw_; }
    const ZonePtrList<Expression>* expressions() const { return &expressions_; }
    int position() const { return pos_; }

    void AddTemplateSpan(const AstRawString* cooked, const AstRawString* raw,
                         int end, Zone* zone) {
      DCHECK_NOT_NULL(raw);
      cooked_.Add(cooked, zone);
      raw_.Add(raw, zone);
    }

    void AddExpression(Expression* expression, Zone* zone) {
      expressions_.Add(expression, zone);
    }

   private:
    ZonePtrList<const AstRawString> cooked_;
    ZonePtrList<const AstRawString> raw_;
    ZonePtrList<Expression> expressions_;
    int pos_;
  };

  using TemplateLiteralState = TemplateLiteral*;

  TemplateLiteralState OpenTemplateLiteral(int pos);
  // "should_cook" means that the span can be "cooked": in tagged template
  // literals, both the raw and "cooked" representations are available to user
  // code ("cooked" meaning that escape sequences are converted to their
  // interpreted values). Invalid escape sequences cause the cooked span
  // to be represented by undefined, instead of being a syntax error.
  // "tail" indicates that this span is the last in the literal.
  void AddTemplateSpan(TemplateLiteralState* state, bool should_cook,
                       bool tail);
  void AddTemplateExpression(TemplateLiteralState* state,
                             Expression* expression);
  Expression* CloseTemplateLiteral(TemplateLiteralState* state, int start,
                                   Expression* tag);

  Expression* RewriteSuperCall(Expression* call_expression);

  void SetLanguageMode(Scope* scope, LanguageMode mode);
#if V8_ENABLE_WEBASSEMBLY
  void SetAsmModule();
#endif  // V8_ENABLE_WEBASSEMBLY

  Expression* RewriteSpreads(ArrayLiteral* lit);

  Expression* BuildInitialYield(int pos, FunctionKind kind);
  Assignment* BuildCreateJSGeneratorObject(int pos, FunctionKind kind);

  // Generic AST generator for throwing errors from compiled code.
  Expression* NewThrowError(Runtime::FunctionId function_id,
                            MessageTemplate message, const AstRawString* arg,
                            int pos);

  void AddArrowFunctionFormalParameters(ParserFormalParameters* parameters,
                                        Expression* params, int end_pos);
  void SetFunctionName(Expression* value, const AstRawString* name,
                       const AstRawString* prefix = nullptr);

  // Helper functions for recursive descent.
  V8_INLINE bool IsEval(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->eval_string();
  }

  V8_INLINE bool IsAsync(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->async_string();
  }

  V8_INLINE bool IsArguments(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->arguments_string();
  }

  V8_INLINE bool IsEvalOrArguments(const AstRawString* identifier) const {
    return IsEval(identifier) || IsArguments(identifier);
  }

  // Returns true if the expression is of type "this.foo".
  V8_INLINE static bool IsThisProperty(Expression* expression) {
    DCHECK_NOT_NULL(expression);
    Property* property = expression->AsProperty();
    return property != nullptr && property->obj()->IsThisExpression();
  }

  // Returns true if the expression is of type "obj.#foo" or "obj?.#foo".
  V8_INLINE static bool IsPrivateReference(Expression* expression) {
    DCHECK_NOT_NULL(expression);
    Property* property = expression->AsProperty();
    if (expression->IsOptionalChain()) {
      Expression* expr_inner = expression->AsOptionalChain()->expression();
      property = expr_inner->AsProperty();
    }
    return property != nullptr && property->IsPrivateReference();
  }

  // This returns true if the expression is an identifier (wrapped
  // inside a variable proxy).  We exclude the case of 'this', which
  // has been converted to a variable proxy.
  V8_INLINE static bool IsIdentifier(Expression* expression) {
    VariableProxy* operand = expression->AsVariableProxy();
    return operand != nullptr && !operand->is_new_target();
  }

  V8_INLINE static const AstRawString* AsIdentifier(Expression* expression) {
    DCHECK(IsIdentifier(expression));
    return expression->AsVariableProxy()->raw_name();
  }

  V8_INLINE VariableProxy* AsIdentifierExpression(Expression* expression) {
    return expression->AsVariableProxy();
  }

  V8_INLINE bool IsConstructor(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->constructor_string();
  }

  V8_INLINE static bool IsBoilerplateProperty(
      ObjectLiteral::Property* property) {
    return !property->IsPrototype();
  }

  V8_INLINE v8::Extension* extension() const { return info_->extension(); }

  V8_INLINE bool ParsingExtension() const { return extension() != nullptr; }

  V8_INLINE bool IsNative(Expression* expr) const {
    DCHECK_NOT_NULL(expr);
    return expr->IsVariableProxy() &&
           expr->AsVariableProxy()->raw_name() ==
               ast_value_factory()->native_string();
  }

  V8_INLINE static bool IsArrayIndex(const AstRawString* string,
                                     uint32_t* index) {
    return string->AsArrayIndex(index);
  }

  // Returns true if the statement is an expression statement containing
  // a single string literal.  If a second argument is given, the literal
  // is also compared with it and the result is true only if they are equal.
  V8_INLINE bool IsStringLiteral(Statement* statement,
                                 const AstRawString* arg = nullptr) const {
    ExpressionStatement* e_stat = statement->AsExpressionStatement();
    if (e_stat == nullptr) return false;
    Literal* literal = e_stat->expression()->AsLiteral();
    if (literal == nullptr || !literal->IsRawString()) return false;
    return arg == nullptr || literal->AsRawString() == arg;
  }

  V8_INLINE void GetDefaultStrings(const AstRawString** default_string,
                                   const AstRawString** dot_default_string) {
    *default_string = ast_value_factory()->default_string();
    *dot_default_string = ast_value_factory()->dot_default_string();
  }

  // Functions for encapsulating the differences between parsing and preparsing;
  // operations interleaved with the recursive descent.
  V8_INLINE void PushLiteralName(const AstRawString* id) {
    fni_.PushLiteralName(id);
  }

  V8_INLINE void PushVariableName(const AstRawString* id) {
    fni_.PushVariableName(id);
  }

  V8_INLINE void PushPropertyName(Expression* expression) {
    if (expression->IsPropertyName()) {
      fni_.PushLiteralName(expression->AsLiteral()->AsRawPropertyName());
    } else {
      fni_.PushLiteralName(ast_value_factory()->computed_string());
    }
  }

  V8_INLINE void PushEnclosingName(const AstRawString* name) {
    fni_.PushEnclosingName(name);
  }

  V8_INLINE void AddFunctionForNameInference(FunctionLiteral* func_to_infer) {
    fni_.AddFunction(func_to_infer);
  }

  V8_INLINE void InferFunctionName() { fni_.Infer(); }

  // If we assign a function literal to a property we pretenure the
  // literal so it can be added as a constant function property.
  V8_INLINE static void CheckAssigningFunctionLiteralToProperty(
      Expression* left, Expression* right) {
    DCHECK_NOT_NULL(left);
    if (left->IsProperty() && right->IsFunctionLiteral()) {
      right->AsFunctionLiteral()->set_pretenure();
    }
  }

  // Returns true if we have a binary expression between two literals. In that
  // case, *x will be changed to an expression which is the computed value.
  bool ShortcutLiteralBinaryExpression(Expression** x, Expression* y,
                                       Token::Value op, int pos);

  bool CollapseConditionalChain(Expression** x, Expression* cond,
                                Expression* then_expression,
                                Expression* else_expression, int pos,
                                const SourceRange& then_range);

  void AppendConditionalChainElse(Expression** x,
                                  const SourceRange& else_range);

  // Returns true if we have a binary operation between a binary/n-ary
  // expression (with the same operation) and a value, which can be collapsed
  // into a single n-ary expression. In that case, *x will be changed to an
  // n-ary expression.
  bool CollapseNaryExpression(Expression** x, Expression* y, Token::Value op,
                              int pos, const SourceRange& range);

  // Returns a UnaryExpression or, in one of the following cases, a Literal.
  // ! <literal> -> true / false
  // + <Number literal> -> <Number literal>
  // - <Number literal> -> <Number literal with value negated>
  // ~ <literal> -> true / false
  Expression* BuildUnaryExpression(Expression* expression, Token::Value op,
                                   int pos);

  // Generate AST node that throws a ReferenceError with the given type.
  V8_INLINE Expression* NewThrowReferenceError(MessageTemplate message,
                                               int pos) {
    return NewThrowError(Runtime::kNewReferenceError, message,
                         ast_value_factory()->empty_string(), pos);
  }

  // Dummy implementation. The parser should never have a unidentifiable
  // error.
  V8_INLINE void ReportUnidentifiableError() { UNREACHABLE(); }

  const AstRawString* GetRawNameFromIdentifier(const AstRawString* arg) {
    return arg;
  }

  const AstRawString* PreParserIdentifierToAstRawString(
      const PreParserIdentifier& arg) {
    // This method definition is only needed due to an MSVC oddity that
    // instantiates the method despite it being unused. See crbug.com/v8/12266 .
    UNREACHABLE();
  }

  IterationStatement* AsIterationStatement(BreakableStatement* s) {
    return s->AsIterationStatement();
  }

  void ReportUnexpectedTokenAt(
      Scanner::Location location, Token::Value token,
      MessageTemplate message = MessageTemplate::kUnexpectedToken);

  // "null" return type creators.
  V8_INLINE static std::nullptr_t NullIdentifier() { return nullptr; }
  V8_INLINE static std::nullptr_t NullExpression() { return nullptr; }
  V8_INLINE static std::nullptr_t NullLiteralProperty() { return nullptr; }
  V8_INLINE static ZonePtrList<Expression>* NullExpressionList() {
    return nullptr;
  }
  V8_INLINE static ZonePtrList<Statement>* NullStatementList() {
    return nullptr;
  }
  V8_INLINE static std::nullptr_t NullStatement() { return nullptr; }
  V8_INLINE static std::nullptr_t NullBlock() { return nullptr; }
  Expression* FailureExpression() { return factory()->FailureExpression(); }

  template <typename T>
  V8_INLINE static bool IsNull(T subject) {
    return subject == nullptr;
  }

  V8_INLINE static bool IsIterationStatement(Statement* subject) {
    return subject->AsIterationStatement() != nullptr;
  }

  // Non-null empty string.
  V8_INLINE const AstRawString* EmptyIdentifierString() const {
    return ast_value_factory()->empty_string();
  }
  V8_INLINE bool IsEmptyIdentifier(const AstRawString* subject) const {
    DCHECK_NOT_NULL(subject);
    return subject->IsEmpty();
  }

  // Producing data during the recursive descent.
  V8_INLINE const AstRawString* GetSymbol() const {
    const AstRawString* result = scanner()->CurrentSymbol(ast_value_factory());
    DCHECK_NOT_NULL(result);
    return result;
  }

  V8_INLINE const AstRawString* GetIdentifier() const { return GetSymbol(); }

  V8_INLINE const AstRawString* GetNextSymbol() const {
    return scanner()->NextSymbol(ast_value_factory());
  }

  V8_INLINE const AstRawString* GetNumberAsSymbol() const {
    double double_value = scanner()->DoubleValue();
    char array[100];
    std::string_view string =
        DoubleToStringView(double_value, base::ArrayVector(array));
    return ast_value_factory()->GetOneByteString(
        base::OneByteVector(string.data(), string.length()));
  }

  const AstRawString* GetBigIntAsSymbol();

  class ThisExpression* ThisExpression() {
    UseThis();
    return factory()->ThisExpression();
  }

  class ThisExpression* NewThisExpression(int pos) {
    UseThis();
    return factory()->NewThisExpression(pos);
  }

  Expression* NewSuperPropertyReference(int pos);
  SuperCallReference* NewSuperCallReference(int pos);
  Expression* NewTargetExpression(int pos);
  Expression* ImportMetaExpression(int pos);

  Expression* ExpressionFromLiteral(Token::Value token, int pos);

  V8_INLINE VariableProxy* ExpressionFromPrivateName(
      PrivateNameScopeIterator* private_name_scope, const AstRawString* name,
      int start_position) {
    VariableProxy* proxy = factory()->ast_node_factory()->NewVariableProxy(
        name, NORMAL_VARIABLE, start_position);
    private_name_scope->AddUnresolvedPrivateName(proxy);
    return proxy;
  }

  V8_INLINE VariableProxy* ExpressionFromIdentifier(
      const AstRawString* name, int start_position,
      InferName infer = InferName::kYes) {
    if (infer == InferName::kYes) {
      fni_.PushVariableName(name);
    }
    return expression_scope()->NewVariable(name, start_position);
  }

  V8_INLINE void DeclareIdentifier(const AstRawString* name,
                                   int start_position) {
    expression_scope()->Declare(name, start_position);
  }

  V8_INLINE Variable* DeclareCatchVariableName(Scope* scope,
                                               const AstRawString* name) {
    return scope->DeclareCatchVariableName(name);
  }

  V8_INLINE ZonePtrList<ClassLiteral::Property>* NewClassPropertyList(
      int size) const {
    return zone()->New<ZonePtrList<ClassLiteral::Property>>(size, zone());
  }
  V8_INLINE ZonePtrList<ClassLiteral::StaticElement>* NewClassStaticElementList(
      int size) const {
    return zone()->New<ZonePtrList<ClassLiteral::StaticElement>>(size, zone());
  }

  Expression* NewV8Intrinsic(const AstRawString* name,
                             const ScopedPtrList<Expression>& args, int pos);

  Expression* NewV8RuntimeFunctionForFuzzing(
      const Runtime::Function* function, const ScopedPtrList<Expression>& args,
      int pos);

  V8_INLINE Statement* NewThrowStatement(Expression* exception, int pos) {
    return factory()->NewExpressionStatement(
        factory()->NewThrow(exception, pos), pos);
  }

  V8_INLINE void AddFormalParameter(ParserFormalParameters* parameters,
                                    Expression* pattern,
                                    Expression* initializer,
                                    int initializer_end_position,
                                    bool is_rest) {
    parameters->UpdateArityAndFunctionLength(initializer != nullptr, is_rest);
    auto parameter =
        parameters->scope->zone()->New<ParserFormalParameters::Parameter>(
            pattern, initializer, scanner()->location().beg_pos,
            initializer_end_position, is_rest);

    parameters->params.Add(parameter);
  }

  V8_INLINE void DeclareFormalParameters(ParserFormalParameters* parameters) {
    bool is_simple = parameters->is_simple;
    DeclarationScope* scope = parameters->scope;
    if (!is_simple) scope->MakeParametersNonSimple();
    for (auto parameter : parameters->params) {
      bool is_optional = parameter->initializer() != nullptr;
      // If the parameter list is simple, declare the parameters normally with
      // their names. If the parameter list is not simple, declare a temporary
      // for each parameter - the corresponding named variable is declared by
      // BuildParameterInitializationBlock.
      scope->DeclareParameter(
          is_simple ? parameter->name() : ast_value_factory()->empty_string(),
          is_simple ? VariableMode::kVar : VariableMode::kTemporary,
          is_optional, parameter->is_rest(), ast_value_factory(),
          parameter->position);
    }
  }

  void ReindexArrowFunctionFormalParameters(ParserFormalParameters* parameters);
  void ReindexComputedMemberName(Expression* computed_name);
  void DeclareArrowFunctionFormalParameters(
      ParserFormalParameters* parameters, Expression* params,
      const Scanner::Location& params_loc);

  Expression* ExpressionListToExpression(const ScopedPtrList<Expression>& args);

  void SetFunctionNameFromPropertyName(LiteralProperty* property,
                                       const AstRawString* name,
                                       const AstRawString* prefix = nullptr);
  void SetFunctionNameFromPropertyName(ObjectLiteralProperty* property,
                                       const AstRawString* name,
                                       const AstRawString* prefix = nullptr);

  void SetFunctionNameFromIdentifierRef(Expression* value,
                                        Expression* identifier);

  V8_INLINE void CountUsage(v8::Isolate::UseCounterFeature feature) {
    ++use_counts_[feature];
  }

  // Returns true iff we're parsing the first function literal during
  // CreateDynamicFunction().
  V8_INLINE bool ParsingDynamicFunctionDeclaration() const {
    return parameters_end_pos_ != kNoSourcePosition;
  }

  V8_INLINE void ConvertBinaryToNaryOperationSourceRange(
      BinaryOperation* binary_op, NaryOperation* nary_op) {
    if (source_range_map_ == nullptr) return;
    DCHECK_NULL(source_range_map_->Find(nary_op));

    BinaryOperationSourceRanges* ranges =
        static_cast<BinaryOperationSourceRanges*>(
            source_range_map_->Find(binary_op));
    if (ranges == nullptr) return;

    SourceRange range = ranges->GetRange(SourceRangeKind::kRight);
    source_range_map_->Insert(
        nary_op, zone()->New<NaryOperationSourceRanges>(zone(), range));
  }

  V8_INLINE void AppendNaryOperationSourceRange(NaryOperation* node,
                                                const SourceRange& range) {
    if (source_range_map_ == nullptr) return;
    NaryOperationSourceRanges* ranges =
        static_cast<NaryOperationSourceRanges*>(source_range_map_->Find(node));
    if (ranges == nullptr) return;

    ranges->AddRange(range);
    DCHECK_EQ(node->subsequent_length(), ranges->RangeCount());
  }

  V8_INLINE void RecordBlockSourceRange(Block* node,
                                        int32_t continuation_position) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node, zone()->New<BlockSourceRanges>(continuation_position));
  }

  V8_INLINE void RecordCaseClauseSourceRange(CaseClause* node,
                                             const SourceRange& body_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(node,
                              zone()->New<CaseClauseSourceRanges>(body_range));
  }

  V8_INLINE void AppendConditionalChainSourceRange(ConditionalChain* node,
                                                   const SourceRange& range) {
    if (source_range_map_ == nullptr) return;
    ConditionalChainSourceRanges* ranges =
        static_cast<ConditionalChainSourceRanges*>(
            source_range_map_->Find(node));
    if (ranges == nullptr) {
      source_range_map_->Insert(
          node, zone()->New<ConditionalChainSourceRanges>(zone()));
    }
    ranges = static_cast<ConditionalChainSourceRanges*>(
        source_range_map_->Find(node));
    if (ranges == nullptr) return;
    ranges->AddThenRanges(range);
    DCHECK_EQ(node->conditional_chain_length(), ranges->RangeCount());
  }

  V8_INLINE void AppendConditionalChainElseSourceRange(
      ConditionalChain* node, const SourceRange& range) {
    if (source_range_map_ == nullptr) return;
    ConditionalChainSourceRanges* ranges =
        static_cast<ConditionalChainSourceRanges*>(
            source_range_map_->Find(node));
    if (ranges == nullptr) return;
    ranges->AddElseRange(range);
  }

  V8_INLINE void RecordConditionalSourceRange(Expression* node,
                                              const SourceRange& then_range,
                                              const SourceRange& else_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node->AsConditional(),
        zone()->New<ConditionalSourceRanges>(then_range, else_range));
  }

  V8_INLINE void RecordFunctionLiteralSourceRange(FunctionLiteral* node) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(node, zone()->New<FunctionLiteralSourceRanges>());
  }

  V8_INLINE void RecordBinaryOperationSourceRange(
      Expression* node, const SourceRange& right_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node->AsBinaryOperation(),
        zone()->New<BinaryOperationSourceRanges>(right_range));
  }

  V8_INLINE void RecordJumpStatementSourceRange(Statement* node,
                                                int32_t continuation_position) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        static_cast<JumpStatement*>(node),
        zone()->New<JumpStatementSourceRanges>(continuation_position));
  }

  V8_INLINE void RecordIfStatementSourceRange(Statement* node,
                                              const SourceRange& then_range,
                                              const SourceRange& else_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node->AsIfStatement(),
        zone()->New<IfStatementSourceRanges>(then_range, else_range));
  }

  V8_INLINE void RecordIterationStatementSourceRange(
      IterationStatement* node, const SourceRange& body_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node, zone()->New<IterationStatementSourceRanges>(body_range));
  }

  // Used to record source ranges of expressions associated with optional chain:
  V8_INLINE void RecordExpressionSourceRange(Expression* node,
                                             const SourceRange& right_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(node,
                              zone()->New<ExpressionSourceRanges>(right_range));
  }

  V8_INLINE void RecordSuspendSourceRange(Expression* node,
                                          int32_t continuation_position) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        static_cast<Suspend*>(node),
        zone()->New<SuspendSourceRanges>(continuation_position));
  }

  V8_INLINE void RecordSwitchStatementSourceRange(
      Statement* node, int32_t continuation_position) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node->AsSwitchStatement(),
        zone()->New<SwitchStatementSourceRanges>(continuation_position));
  }

  V8_INLINE void RecordThrowSourceRange(Statement* node,
                                        int32_t continuation_position) {
    if (source_range_map_ == nullptr) return;
    ExpressionStatement* expr_stmt = static_cast<ExpressionStatement*>(node);
    Throw* throw_expr = expr_stmt->expression()->AsThrow();
    source_range_map_->Insert(
        throw_expr, zone()->New<ThrowSourceRanges>(continuation_position));
  }

  V8_INLINE void RecordTryCatchStatementSourceRange(
      TryCatchStatement* node, const SourceRange& body_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node, zone()->New<TryCatchStatementSourceRanges>(body_range));
  }

  V8_INLINE void RecordTryFinallyStatementSourceRange(
      TryFinallyStatement* node, const SourceRange& body_range) {
    if (source_range_map_ == nullptr) return;
    source_range_map_->Insert(
        node, zone()->New<TryFinallyStatementSourceRanges>(body_range));
  }

  V8_INLINE FunctionLiteral::EagerCompileHint GetEmbedderCompileHint(
      FunctionLiteral::EagerCompileHint current_compile_hint, int position) {
    if (current_compile_hint == FunctionLiteral::kShouldLazyCompile) {
      v8::CompileHintCallback callback = info_->compile_hint_callback();
      if (callback != nullptr &&
          callback(position, info_->compile_hint_callback_data())) {
        return FunctionLiteral::kShouldEagerCompile;
      }
    }
    return current_compile_hint;
  }

  // Generate the next internal variable name for binding an exported namespace
  // object (used to implement the "export * as" syntax).
  const AstRawString* NextInternalNamespaceExportName();

  ParseInfo* info() const { return info_; }

  std::vector<uint8_t>* preparse_data_buffer() {
    return &preparse_data_buffer_;
  }

  // Parser's private field members.
  friend class PreParserZoneScope;  // Uses reusable_preparser().
  friend class PreparseDataBuilder;  // Uses preparse_data_buffer()

  LocalIsolate* local_isolate_;
  ParseInfo* info_;
  Scanner scanner_;
  Zone preparser_zone_;
  PreParser* reusable_preparser_;
  Mode mode_;

  MaybeHandle<FixedArray> maybe_wrapped_arguments_;

  SourceRangeMap* source_range_map_ = nullptr;

  friend class ParserTargetScope;

  ScriptCompiler::CompileOptions compile_options_;

  // For NextInternalNamespaceExportName().
  int number_of_named_namespace_exports_ = 0;

  // Other information which will be stored in Parser and moved to Isolate after
  // parsing.
  int use_counts_[v8::Isolate::kUseCounterFeatureCount];
  int total_preparse_skipped_;
  bool allow_lazy_;
  bool temp_zoned_;
  ConsumedPreparseData* consumed_preparse_data_;
  std::vector<uint8_t> preparse_data_buffer_;

  // If not kNoSourcePosition, indicates that the first function literal
  // encountered is a dynamic function, see CreateDynamicFunction(). This field
  // indicates the correct position of the ')' that closes the parameter list.
  // After that ')' is encountered, this field is reset to kNoSourcePosition.
  int parameters_end_pos_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PARSER_H_

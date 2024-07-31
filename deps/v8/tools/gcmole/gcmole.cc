// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// This is clang plugin used by gcmole tool. See README for more details.

#include <bitset>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stack>

#include "clang/AST/APValue.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

namespace {

bool g_tracing_enabled = false;
bool g_dead_vars_analysis = false;
bool g_verbose = false;
bool g_print_gc_call_chain = false;

#define TRACE(str)                   \
  do {                               \
    if (g_tracing_enabled) {         \
      std::cout << str << std::endl; \
    }                                \
  } while (false)

#define TRACE_LLVM_TYPE(str, type)                                \
  do {                                                            \
    if (g_tracing_enabled) {                                      \
      std::cout << str << " " << type.getAsString() << std::endl; \
    }                                                             \
  } while (false)

// Node: The following is used when tracing --dead-vars
// to provide extra info for the GC suspect.
#define TRACE_LLVM_DECL(str, decl)                   \
  do {                                               \
    if (g_tracing_enabled && g_dead_vars_analysis) { \
      std::cout << str << std::endl;                 \
      decl->dump();                                  \
    }                                                \
  } while (false)

typedef std::string MangledName;
typedef std::set<MangledName> CalleesSet;
typedef std::map<MangledName, MangledName> CalleesMap;

static bool GetMangledName(clang::MangleContext* ctx,
                           const clang::NamedDecl* decl, MangledName* result) {
  if (llvm::isa<clang::CXXConstructorDecl>(decl)) return false;
  if (llvm::isa<clang::CXXDestructorDecl>(decl)) return false;
  if (llvm::isa<clang::CXXDeductionGuideDecl>(decl)) return false;
  llvm::SmallVector<char, 512> output;
  llvm::raw_svector_ostream out(output);
  ctx->mangleName(decl, out);
  *result = out.str().str();
  return true;
}

static bool InV8Namespace(const clang::NamedDecl* decl) {
  return decl->getQualifiedNameAsString().compare(0, 4, "v8::") == 0;
}

static std::string EXTERNAL("EXTERNAL");
static std::string STATE_TAG("enum v8::internal::StateTag");

static bool IsExternalVMState(const clang::ValueDecl* var) {
  const clang::EnumConstantDecl* enum_constant =
      llvm::dyn_cast<clang::EnumConstantDecl>(var);
  if (enum_constant != nullptr &&
      enum_constant->getNameAsString() == EXTERNAL) {
    clang::QualType type = enum_constant->getType();
    return (type.getAsString() == STATE_TAG);
  }

  return false;
}

struct Resolver {
  explicit Resolver(clang::ASTContext& ctx)
      : ctx_(ctx), decl_ctx_(ctx.getTranslationUnitDecl()) {}

  Resolver(clang::ASTContext& ctx, clang::DeclContext* decl_ctx)
      : ctx_(ctx), decl_ctx_(decl_ctx) {}

  clang::DeclarationName ResolveName(const char* n) {
    clang::IdentifierInfo* ident = &ctx_.Idents.get(n);
    return ctx_.DeclarationNames.getIdentifier(ident);
  }

  Resolver ResolveNamespace(const char* n) {
    return Resolver(ctx_, Resolve<clang::NamespaceDecl>(n));
  }

  template <typename T>
  T* Resolve(const char* n) {
    if (decl_ctx_ == nullptr) return nullptr;

    clang::DeclContext::lookup_result result =
        decl_ctx_->lookup(ResolveName(n));

    clang::DeclContext::lookup_iterator end = result.end();
    for (clang::DeclContext::lookup_iterator i = result.begin(); i != end;
         i++) {
      clang::NamedDecl* decl = *i;

      // Try to strip off any type aliases.
      const clang::TypeAliasDecl* type_alias_decl =
          llvm::dyn_cast_or_null<clang::TypeAliasDecl>(decl);
      if (type_alias_decl) {
        clang::QualType underlying_type = type_alias_decl->getUnderlyingType();
        clang::QualType desugared_type = underlying_type.getDesugaredType(ctx_);
        clang::TagDecl* tag_decl = desugared_type->getAsTagDecl();
        if (!tag_decl) {
          llvm::errs() << "Couldn't resolve target decl of type alias "
                       << decl->getNameAsString() << "\n";
          decl->dump();
          return nullptr;
        }
        decl = tag_decl;
      }

      if (llvm::isa<T>(decl)) {
        return llvm::cast<T>(decl);
      }

      llvm::errs() << "Didn't match declaration template for " << n
                   << " against " << decl->getNameAsString() << "\n";
      decl->dump();
    }

    return nullptr;
  }

 private:
  clang::ASTContext& ctx_;
  clang::DeclContext* decl_ctx_;
};

class CalleesPrinter : public clang::RecursiveASTVisitor<CalleesPrinter> {
 public:
  explicit CalleesPrinter(clang::MangleContext* ctx) : ctx_(ctx) {}

  virtual bool VisitCallExpr(clang::CallExpr* expr) {
    const clang::FunctionDecl* callee = expr->getDirectCallee();
    if (callee != nullptr) AnalyzeFunction(callee);
    return true;
  }

  virtual bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
    // If function mentions EXTERNAL VMState add artificial garbage collection
    // mark.
    if (IsExternalVMState(expr->getDecl())) {
      AddCallee("CollectGarbage", "CollectGarbage");
    }
    return true;
  }

  void AnalyzeFunction(const clang::FunctionDecl* f) {
    if (!InV8Namespace(f)) return;
    MangledName name;
    if (!GetMangledName(ctx_, f, &name)) return;
    const std::string& function = f->getNameAsString();
    AddCallee(name, function);

    const clang::FunctionDecl* body = nullptr;
    if (f->hasBody(body) && !Analyzed(name)) {
      EnterScope(name);
      TraverseStmt(body->getBody());
      LeaveScope();
    }
  }

  typedef std::map<MangledName, CalleesSet*> Callgraph;

  bool Analyzed(const MangledName& name) { return callgraph_[name] != nullptr; }

  void EnterScope(const MangledName& name) {
    CalleesSet* callees = callgraph_[name];

    if (callees == nullptr) {
      callgraph_[name] = callees = new CalleesSet();
    }

    scopes_.push(callees);
  }

  void LeaveScope() { scopes_.pop(); }

  void AddCallee(const MangledName& name, const MangledName& function) {
    if (!scopes_.empty()) scopes_.top()->insert(name);
    mangled_to_function_[name] = function;
  }

  void PrintCallGraph() {
    for (Callgraph::const_iterator i = callgraph_.begin(), e = callgraph_.end();
         i != e; ++i) {
      std::cout << i->first << "," << mangled_to_function_[i->first] << "\n";

      CalleesSet* callees = i->second;
      for (CalleesSet::const_iterator j = callees->begin(), e = callees->end();
           j != e; ++j) {
        std::cout << "\t" << *j << "," << mangled_to_function_[*j] << "\n";
      }
    }
  }

 private:
  clang::MangleContext* ctx_;

  std::stack<CalleesSet*> scopes_;
  Callgraph callgraph_;
  CalleesMap mangled_to_function_;
};

class FunctionDeclarationFinder
    : public clang::ASTConsumer,
      public clang::RecursiveASTVisitor<FunctionDeclarationFinder> {
 public:
  explicit FunctionDeclarationFinder(
      clang::DiagnosticsEngine& diagnostics_engine,
      clang::SourceManager& source_manager,
      const std::vector<std::string>& args)
      : diagnostics_engine_(diagnostics_engine),
        source_manager_(source_manager) {}

  void HandleTranslationUnit(clang::ASTContext& ctx) override {
    mangle_context_ =
        clang::ItaniumMangleContext::create(ctx, diagnostics_engine_);
    callees_printer_ = new CalleesPrinter(mangle_context_);
    TraverseDecl(ctx.getTranslationUnitDecl());
    callees_printer_->PrintCallGraph();
  }

  virtual bool VisitFunctionDecl(clang::FunctionDecl* decl) {
    callees_printer_->AnalyzeFunction(decl);
    return true;
  }

 private:
  clang::DiagnosticsEngine& diagnostics_engine_;
  clang::SourceManager& source_manager_;
  clang::MangleContext* mangle_context_;

  CalleesPrinter* callees_printer_;
};

static bool gc_suspects_loaded = false;
static CalleesSet gc_suspects;
static CalleesSet gc_functions;

static bool allowlist_loaded = false;
static CalleesSet suspects_allowlist;

static bool gc_causes_loaded = false;
static std::map<MangledName, std::vector<MangledName>> gc_causes;

static void LoadGCCauses() {
  if (gc_causes_loaded) return;
  std::ifstream fin("gccauses");
  std::string mangled, function;
  while (!fin.eof()) {
    std::getline(fin, mangled, ',');
    std::getline(fin, function);
    if (mangled.empty()) break;
    std::string parent = mangled;
    // start,nested
    std::getline(fin, mangled, ',');
    assert(mangled.compare("start") == 0);
    std::getline(fin, function);
    assert(function.compare("nested") == 0);
    while (true) {
      std::getline(fin, mangled, ',');
      std::getline(fin, function);
      if (mangled.compare("end") == 0) {
        assert(function.compare("nested") == 0);
        break;
      }
      gc_causes[parent].push_back(mangled);
    }
  }
  gc_causes_loaded = true;
}

static void LoadGCSuspects() {
  if (gc_suspects_loaded) return;

  std::ifstream fin("gcsuspects");
  std::string mangled, function;

  while (!fin.eof()) {
    std::getline(fin, mangled, ',');
    gc_suspects.insert(mangled);
    std::getline(fin, function);
    gc_functions.insert(function);
  }

  gc_suspects_loaded = true;
}

static void LoadSuspectsAllowList() {
  if (allowlist_loaded) return;

  // TODO(cbruni): clean up once fully migrated
  std::ifstream fin("tools/gcmole/suspects.allowlist");
  std::string s;

  while (fin >> s) suspects_allowlist.insert(s);

  allowlist_loaded = true;
}

// Looks for exact match of the mangled name.
static bool IsKnownToCauseGC(clang::MangleContext* ctx,
                             const clang::FunctionDecl* decl) {
  LoadGCSuspects();
  if (!InV8Namespace(decl)) return false;
  if (suspects_allowlist.find(decl->getNameAsString()) !=
      suspects_allowlist.end()) {
    return false;
  }
  MangledName name;
  if (GetMangledName(ctx, decl, &name)) {
    return gc_suspects.find(name) != gc_suspects.end();
  }
  return false;
}

// Looks for partial match of only the function name.
static bool IsSuspectedToCauseGC(clang::MangleContext* ctx,
                                 const clang::FunctionDecl* decl) {
  LoadGCSuspects();
  if (!InV8Namespace(decl)) return false;
  LoadSuspectsAllowList();
  if (suspects_allowlist.find(decl->getNameAsString()) !=
      suspects_allowlist.end()) {
    return false;
  }
  if (gc_functions.find(decl->getNameAsString()) != gc_functions.end()) {
    TRACE_LLVM_DECL("Suspected by ", decl);
    return true;
  }
  return false;
}

static const int kNoEffect = 0;
static const int kCausesGC = 1;
static const int kRawDef = 2;
static const int kRawUse = 4;
static const int kAllEffects = kCausesGC | kRawDef | kRawUse;

class Environment;

class ExprEffect {
 public:
  bool hasGC() { return (effect_ & kCausesGC) != 0; }
  void setGC() { effect_ |= kCausesGC; }

  bool hasRawDef() { return (effect_ & kRawDef) != 0; }
  void setRawDef() { effect_ |= kRawDef; }

  bool hasRawUse() { return (effect_ & kRawUse) != 0; }
  void setRawUse() { effect_ |= kRawUse; }

  static ExprEffect None() { return ExprEffect(kNoEffect, nullptr); }
  static ExprEffect NoneWithEnv(Environment* env) {
    return ExprEffect(kNoEffect, env);
  }
  static ExprEffect RawUse() { return ExprEffect(kRawUse, nullptr); }

  static ExprEffect Merge(ExprEffect a, ExprEffect b);
  static ExprEffect MergeSeq(ExprEffect a, ExprEffect b);
  ExprEffect Define(const std::string& name);

  Environment* env() {
    return reinterpret_cast<Environment*>(effect_ & ~kAllEffects);
  }

  static ExprEffect GC() { return ExprEffect(kCausesGC, nullptr); }

 private:
  ExprEffect(int effect, Environment* env)
      : effect_((effect & kAllEffects) | reinterpret_cast<intptr_t>(env)) {}

  intptr_t effect_;
};

const std::string BAD_EXPR_MSG(
    "Possible problem with evaluation order with interleaved GCs.");
const std::string DEAD_VAR_MSG("Possibly stale variable due to GCs.");

class Environment {
 public:
  Environment() = default;

  static Environment Unreachable() {
    Environment env;
    env.unreachable_ = true;
    return env;
  }

  static Environment Merge(const Environment& l, const Environment& r) {
    Environment out(l);
    out &= r;
    return out;
  }

  Environment ApplyEffect(ExprEffect effect) const {
    Environment out = effect.hasGC() ? Environment() : Environment(*this);
    if (effect.env()) out |= *effect.env();
    return out;
  }

  typedef std::map<std::string, int> SymbolTable;

  bool IsAlive(const std::string& name) const {
    SymbolTable::iterator code = symbol_table_.find(name);
    if (code == symbol_table_.end()) return false;
    return is_live(code->second);
  }

  bool Equal(const Environment& env) {
    if (unreachable_ && env.unreachable_) return true;
    size_t size = std::max(live_.size(), env.live_.size());
    for (size_t i = 0; i < size; ++i) {
      if (is_live(i) != env.is_live(i)) return false;
    }
    return true;
  }

  Environment Define(const std::string& name) const {
    return Environment(*this, SymbolToCode(name));
  }

  void MDefine(const std::string& name) { set_live(SymbolToCode(name)); }

  static int SymbolToCode(const std::string& name) {
    SymbolTable::iterator code = symbol_table_.find(name);

    if (code == symbol_table_.end()) {
      int new_code = symbol_table_.size();
      symbol_table_.insert(std::make_pair(name, new_code));
      return new_code;
    }

    return code->second;
  }

  static void ClearSymbolTable() {
    for (Environment* e : envs_) delete e;
    envs_.clear();
    symbol_table_.clear();
  }

  void Print() const {
    bool comma = false;
    std::cout << "{";
    for (auto& e : symbol_table_) {
      if (!is_live(e.second)) continue;
      if (comma) std::cout << ", ";
      std::cout << e.first;
      comma = true;
    }
    std::cout << "}" << std::endl;
  }

  static Environment* Allocate(const Environment& env) {
    Environment* allocated_env = new Environment(env);
    envs_.push_back(allocated_env);
    return allocated_env;
  }

 private:
  Environment(const Environment& l, int code) : live_(l.live_) {
    set_live(code);
  }

  void set_live(size_t pos) {
    if (unreachable_) return;
    if (pos >= live_.size()) live_.resize(pos + 1);
    live_[pos] = true;
  }

  bool is_live(size_t pos) const {
    return unreachable_ || (live_.size() > pos && live_[pos]);
  }

  Environment& operator|=(const Environment& o) {
    if (o.unreachable_) {
      unreachable_ = true;
      live_.clear();
    } else if (!unreachable_) {
      for (size_t i = 0, e = o.live_.size(); i < e; ++i) {
        if (o.live_[i]) set_live(i);
      }
    }
    return *this;
  }

  Environment& operator&=(const Environment& o) {
    if (o.unreachable_) return *this;
    if (unreachable_) return *this = o;

    // Carry over false bits from the tail of o.live_, and reset all bits that
    // are not set in o.live_.
    size_t size = std::max(live_.size(), o.live_.size());
    if (size > live_.size()) live_.resize(size);
    for (size_t i = 0; i < size; ++i) {
      if (live_[i] && (i >= o.live_.size() || !o.live_[i])) live_[i] = false;
    }
    return *this;
  }

  static SymbolTable symbol_table_;
  static std::vector<Environment*> envs_;

  std::vector<bool> live_;
  // unreachable_ == true implies live_.empty(), but still is_live(i) returns
  // true for all i.
  bool unreachable_ = false;

  friend class ExprEffect;
  friend class CallProps;
};

class CallProps {
 public:
  CallProps() : env_(nullptr) {}

  void SetEffect(int arg, ExprEffect in) {
    if (in.hasGC()) {
      gc_.set(arg);
    }
    if (in.hasRawDef()) raw_def_.set(arg);
    if (in.hasRawUse()) raw_use_.set(arg);
    if (in.env() != nullptr) {
      if (env_ == nullptr) {
        env_ = in.env();
      } else {
        *env_ |= *in.env();
      }
    }
  }

  ExprEffect ComputeCumulativeEffect(bool result_is_raw) {
    ExprEffect out = ExprEffect::NoneWithEnv(env_);
    if (gc_.any()) out.setGC();
    if (raw_use_.any()) out.setRawUse();
    if (result_is_raw) out.setRawDef();
    return out;
  }

  bool IsSafe() {
    if (!gc_.any()) return true;
    std::bitset<kMaxNumberOfArguments> raw = (raw_def_ | raw_use_);
    if (!raw.any()) return true;
    bool result = gc_.count() == 1 && !((raw ^ gc_).any());
    return result;
  }

 private:
  static const int kMaxNumberOfArguments = 64;
  std::bitset<kMaxNumberOfArguments> raw_def_;
  std::bitset<kMaxNumberOfArguments> raw_use_;
  std::bitset<kMaxNumberOfArguments> gc_;
  Environment* env_;
};

Environment::SymbolTable Environment::symbol_table_;
std::vector<Environment*> Environment::envs_;

ExprEffect ExprEffect::Merge(ExprEffect a, ExprEffect b) {
  Environment* a_env = a.env();
  Environment* b_env = b.env();
  Environment* out = nullptr;
  if (a_env != nullptr && b_env != nullptr) {
    out = Environment::Allocate(*a_env);
    *out &= *b_env;
  }
  return ExprEffect(a.effect_ | b.effect_, out);
}

ExprEffect ExprEffect::MergeSeq(ExprEffect a, ExprEffect b) {
  Environment* a_env = b.hasGC() ? nullptr : a.env();
  Environment* b_env = b.env();
  Environment* out = (b_env == nullptr) ? a_env : b_env;
  if (a_env != nullptr && b_env != nullptr) {
    out = Environment::Allocate(*b_env);
    *out |= *a_env;
  }
  return ExprEffect(a.effect_ | b.effect_, out);
}

ExprEffect ExprEffect::Define(const std::string& name) {
  Environment* e = env();
  if (e == nullptr) {
    e = Environment::Allocate(Environment());
  }
  e->MDefine(name);
  return ExprEffect(effect_, e);
}

static std::string THIS("this");

class FunctionAnalyzer {
 public:
  FunctionAnalyzer(clang::MangleContext* ctx,
                   clang::CXXRecordDecl* heap_object_decl,
                   clang::CXXRecordDecl* smi_decl,
                   clang::CXXRecordDecl* tagged_index_decl,
                   clang::ClassTemplateDecl* tagged_decl,
                   clang::CXXRecordDecl* no_gc_mole_decl,
                   clang::DiagnosticsEngine& d, clang::SourceManager& sm)
      : ctx_(ctx),
        heap_object_decl_(heap_object_decl),
        smi_decl_(smi_decl),
        tagged_index_decl_(tagged_index_decl),
        tagged_decl_(tagged_decl),
        no_gc_mole_decl_(no_gc_mole_decl),
        d_(d),
        sm_(sm),
        block_(nullptr) {}

  // --------------------------------------------------------------------------
  // Expressions
  // --------------------------------------------------------------------------

  ExprEffect VisitExpr(clang::Expr* expr, const Environment& env) {
#define VISIT(type)                                                         \
  do {                                                                      \
    clang::type* concrete_expr = llvm::dyn_cast_or_null<clang::type>(expr); \
    if (concrete_expr != nullptr) {                                         \
      return Visit##type(concrete_expr, env);                               \
    }                                                                       \
  } while (0);

    VISIT(AbstractConditionalOperator);
    VISIT(AddrLabelExpr);
    VISIT(ArraySubscriptExpr);
    VISIT(BinaryOperator);
    VISIT(BlockExpr);
    VISIT(CallExpr);
    VISIT(CastExpr);
    VISIT(CharacterLiteral);
    VISIT(ChooseExpr);
    VISIT(CompoundLiteralExpr);
    VISIT(ConstantExpr);
    VISIT(CXXBindTemporaryExpr);
    VISIT(CXXBoolLiteralExpr);
    VISIT(CXXConstructExpr);
    VISIT(CXXDefaultArgExpr);
    VISIT(CXXDeleteExpr);
    VISIT(CXXDependentScopeMemberExpr);
    VISIT(CXXNewExpr);
    VISIT(CXXNoexceptExpr);
    VISIT(CXXNullPtrLiteralExpr);
    VISIT(CXXPseudoDestructorExpr);
    VISIT(CXXScalarValueInitExpr);
    VISIT(CXXThisExpr);
    VISIT(CXXThrowExpr);
    VISIT(CXXTypeidExpr);
    VISIT(CXXUnresolvedConstructExpr);
    VISIT(CXXUuidofExpr);
    VISIT(DeclRefExpr);
    VISIT(DependentScopeDeclRefExpr);
    VISIT(DesignatedInitExpr);
    VISIT(ExprWithCleanups);
    VISIT(ExtVectorElementExpr);
    VISIT(FloatingLiteral);
    VISIT(GNUNullExpr);
    VISIT(ImaginaryLiteral);
    VISIT(ImplicitCastExpr);
    VISIT(ImplicitValueInitExpr);
    VISIT(InitListExpr);
    VISIT(IntegerLiteral);
    VISIT(MaterializeTemporaryExpr);
    VISIT(MemberExpr);
    VISIT(OffsetOfExpr);
    VISIT(OpaqueValueExpr);
    VISIT(OverloadExpr);
    VISIT(PackExpansionExpr);
    VISIT(ParenExpr);
    VISIT(ParenListExpr);
    VISIT(PredefinedExpr);
    VISIT(ShuffleVectorExpr);
    VISIT(SizeOfPackExpr);
    VISIT(StmtExpr);
    VISIT(StringLiteral);
    VISIT(SubstNonTypeTemplateParmPackExpr);
    VISIT(TypeTraitExpr);
    VISIT(UnaryOperator);
    VISIT(UnaryExprOrTypeTraitExpr);
    VISIT(VAArgExpr);
#undef VISIT

    return ExprEffect::None();
  }

#define DECL_VISIT_EXPR(type) \
  ExprEffect Visit##type(clang::type* expr, const Environment& env)

#define IGNORE_EXPR(type)                                             \
  ExprEffect Visit##type(clang::type* expr, const Environment& env) { \
    return ExprEffect::None();                                        \
  }

  IGNORE_EXPR(AddrLabelExpr);
  IGNORE_EXPR(BlockExpr);
  IGNORE_EXPR(CharacterLiteral);
  IGNORE_EXPR(ChooseExpr);
  IGNORE_EXPR(CompoundLiteralExpr);
  IGNORE_EXPR(CXXBoolLiteralExpr);
  IGNORE_EXPR(CXXDependentScopeMemberExpr);
  IGNORE_EXPR(CXXNullPtrLiteralExpr);
  IGNORE_EXPR(CXXPseudoDestructorExpr);
  IGNORE_EXPR(CXXScalarValueInitExpr);
  IGNORE_EXPR(CXXNoexceptExpr);
  IGNORE_EXPR(CXXTypeidExpr);
  IGNORE_EXPR(CXXUnresolvedConstructExpr);
  IGNORE_EXPR(CXXUuidofExpr);
  IGNORE_EXPR(DependentScopeDeclRefExpr);
  IGNORE_EXPR(DesignatedInitExpr);
  IGNORE_EXPR(ExtVectorElementExpr);
  IGNORE_EXPR(FloatingLiteral);
  IGNORE_EXPR(ImaginaryLiteral);
  IGNORE_EXPR(IntegerLiteral);
  IGNORE_EXPR(OffsetOfExpr);
  IGNORE_EXPR(ImplicitValueInitExpr);
  IGNORE_EXPR(PackExpansionExpr);
  IGNORE_EXPR(PredefinedExpr);
  IGNORE_EXPR(ShuffleVectorExpr);
  IGNORE_EXPR(SizeOfPackExpr);
  IGNORE_EXPR(StmtExpr);
  IGNORE_EXPR(StringLiteral);
  IGNORE_EXPR(SubstNonTypeTemplateParmPackExpr);
  IGNORE_EXPR(TypeTraitExpr);
  IGNORE_EXPR(VAArgExpr);
  IGNORE_EXPR(GNUNullExpr);
  IGNORE_EXPR(OverloadExpr);

  DECL_VISIT_EXPR(CXXThisExpr) { return Use(expr, expr->getType(), THIS, env); }

  DECL_VISIT_EXPR(AbstractConditionalOperator) {
    Environment after_cond = env.ApplyEffect(VisitExpr(expr->getCond(), env));
    return ExprEffect::Merge(VisitExpr(expr->getTrueExpr(), after_cond),
                             VisitExpr(expr->getFalseExpr(), after_cond));
  }

  DECL_VISIT_EXPR(ArraySubscriptExpr) {
    clang::Expr* exprs[2] = {expr->getBase(), expr->getIdx()};
    return Parallel(expr, 2, exprs, env);
  }

  bool IsRawPointerVar(clang::Expr* expr, std::string* var_name) {
    if (llvm::isa<clang::DeclRefExpr>(expr)) {
      *var_name =
          llvm::cast<clang::DeclRefExpr>(expr)->getDecl()->getNameAsString();
      return true;
    }

    return false;
  }

  DECL_VISIT_EXPR(BinaryOperator) {
    clang::Expr* lhs = expr->getLHS();
    clang::Expr* rhs = expr->getRHS();
    clang::Expr* exprs[2] = {lhs, rhs};

    switch (expr->getOpcode()) {
      case clang::BO_Comma:
        return Sequential(expr, 2, exprs, env);

      case clang::BO_LAnd:
      case clang::BO_LOr:
        return ExprEffect::Merge(VisitExpr(lhs, env), VisitExpr(rhs, env));

      default:
        return Parallel(expr, 2, exprs, env);
    }
  }

  DECL_VISIT_EXPR(CXXBindTemporaryExpr) {
    return VisitExpr(expr->getSubExpr(), env);
  }

  DECL_VISIT_EXPR(MaterializeTemporaryExpr) {
    return VisitExpr(expr->getSubExpr(), env);
  }

  DECL_VISIT_EXPR(CXXConstructExpr) { return VisitArguments<>(expr, env); }

  DECL_VISIT_EXPR(CXXDefaultArgExpr) { return VisitExpr(expr->getExpr(), env); }

  DECL_VISIT_EXPR(CXXDeleteExpr) { return VisitExpr(expr->getArgument(), env); }

  DECL_VISIT_EXPR(CXXNewExpr) { return VisitExpr(expr->getInitializer(), env); }

  DECL_VISIT_EXPR(ExprWithCleanups) {
    return VisitExpr(expr->getSubExpr(), env);
  }

  DECL_VISIT_EXPR(CXXThrowExpr) { return VisitExpr(expr->getSubExpr(), env); }

  DECL_VISIT_EXPR(ImplicitCastExpr) {
    return VisitExpr(expr->getSubExpr(), env);
  }

  DECL_VISIT_EXPR(ConstantExpr) { return VisitExpr(expr->getSubExpr(), env); }

  DECL_VISIT_EXPR(InitListExpr) {
    return Sequential(expr, expr->getNumInits(), expr->getInits(), env);
  }

  DECL_VISIT_EXPR(MemberExpr) { return VisitExpr(expr->getBase(), env); }

  DECL_VISIT_EXPR(OpaqueValueExpr) {
    return VisitExpr(expr->getSourceExpr(), env);
  }

  DECL_VISIT_EXPR(ParenExpr) { return VisitExpr(expr->getSubExpr(), env); }

  DECL_VISIT_EXPR(ParenListExpr) {
    return Parallel(expr, expr->getNumExprs(), expr->getExprs(), env);
  }

  DECL_VISIT_EXPR(UnaryOperator) {
    // TODO(gcmole): We are treating all expressions that look like
    // {&raw_pointer_var} as definitions of {raw_pointer_var}. This should be
    // changed to recognize less generic pattern:
    //
    //   if (maybe_object->ToObject(&obj)) return maybe_object;
    //
    if (expr->getOpcode() == clang::UO_AddrOf) {
      std::string var_name;
      if (IsRawPointerVar(expr->getSubExpr(), &var_name)) {
        return ExprEffect::None().Define(var_name);
      }
    }
    return VisitExpr(expr->getSubExpr(), env);
  }

  DECL_VISIT_EXPR(UnaryExprOrTypeTraitExpr) {
    if (expr->isArgumentType()) {
      return ExprEffect::None();
    }

    return VisitExpr(expr->getArgumentExpr(), env);
  }

  DECL_VISIT_EXPR(CastExpr) { return VisitExpr(expr->getSubExpr(), env); }

  DECL_VISIT_EXPR(DeclRefExpr) { return Use(expr, expr->getDecl(), env); }

  // Represents a node in the AST {parent} whose children {exprs} have
  // undefined order of evaluation, e.g. array subscript or a binary operator.
  ExprEffect Parallel(clang::Expr* parent, int n, clang::Expr** exprs,
                      const Environment& env) {
    CallProps props;
    for (int i = 0; i < n; ++i) {
      props.SetEffect(i, VisitExpr(exprs[i], env));
    }
    if (!props.IsSafe()) ReportUnsafe(parent, BAD_EXPR_MSG);
    return props.ComputeCumulativeEffect(
        RepresentsRawPointerType(parent->getType()));
  }

  // Represents a node in the AST {parent} whose children {exprs} are
  // executed in sequence, e.g. a switch statement or an initializer list.
  ExprEffect Sequential(clang::Stmt* parent, int n, clang::Expr** exprs,
                        const Environment& env) {
    ExprEffect out = ExprEffect::None();
    Environment out_env = env;
    for (int i = 0; i < n; ++i) {
      out = ExprEffect::MergeSeq(out, VisitExpr(exprs[i], out_env));
      out_env = out_env.ApplyEffect(out);
    }
    return out;
  }

  // Represents a node in the AST {parent} which uses the variable {var_name},
  // e.g. this expression or operator&.
  // Here we observe the type in {var_type} of a previously declared variable
  // and if it's a raw heap object type, we do the following:
  // 1. If it got stale due to GC since its declaration, we report it as such.
  // 2. Mark its raw usage in the ExprEffect returned by this function.
  ExprEffect Use(const clang::Expr* parent, const clang::QualType& var_type,
                 const std::string& var_name, const Environment& env) {
    if (!g_dead_vars_analysis) return ExprEffect::None();
    if (!RepresentsRawPointerType(var_type)) return ExprEffect::None();
    // We currently care only about our internal pointer types and not about
    // raw C++ pointers, because normally special care is taken when storing
    // raw pointers to the managed heap. Furthermore, checking for raw
    // pointers produces too many false positives in the dead variable
    // analysis.
    if (!IsInternalPointerType(var_type)) return ExprEffect::None();
    if (env.IsAlive(var_name)) return ExprEffect::None();
    if (HasActiveGuard()) return ExprEffect::None();
    ReportUnsafe(parent, DEAD_VAR_MSG);
    return ExprEffect::RawUse();
  }

  ExprEffect Use(const clang::Expr* parent, const clang::ValueDecl* var,
                 const Environment& env) {
    if (IsExternalVMState(var)) return ExprEffect::GC();
    return Use(parent, var->getType(), var->getNameAsString(), env);
  }

  template <typename ExprType>
  ExprEffect VisitArguments(ExprType* call, const Environment& env) {
    CallProps props;
    VisitArguments<>(call, &props, env);
    if (!props.IsSafe()) ReportUnsafe(call, BAD_EXPR_MSG);
    return props.ComputeCumulativeEffect(
        RepresentsRawPointerType(call->getType()));
  }

  template <typename ExprType>
  void VisitArguments(ExprType* call, CallProps* props,
                      const Environment& env) {
    for (unsigned arg = 0; arg < call->getNumArgs(); arg++) {
      props->SetEffect(arg + 1, VisitExpr(call->getArg(arg), env));
    }
  }

  // After visiting the receiver and the arguments of the {call} node, this
  // function might report a GC-unsafe usage (due to the undefined evaluation
  // order of the receiver and the rest of the arguments).
  ExprEffect VisitCallExpr(clang::CallExpr* call, const Environment& env) {
    CallProps props;

    clang::CXXMemberCallExpr* memcall =
        llvm::dyn_cast_or_null<clang::CXXMemberCallExpr>(call);
    if (memcall != nullptr) {
      clang::Expr* receiver = memcall->getImplicitObjectArgument();
      props.SetEffect(0, VisitExpr(receiver, env));
    }

    std::string var_name;
    clang::CXXOperatorCallExpr* opcall =
        llvm::dyn_cast_or_null<clang::CXXOperatorCallExpr>(call);
    if (opcall != nullptr && opcall->isAssignmentOp() &&
        IsRawPointerVar(opcall->getArg(0), &var_name)) {
      // TODO(gcmole): We are treating all assignment operator calls with
      // the left hand side looking like {raw_pointer_var} as safe independent
      // of the concrete assignment operator implementation. This should be
      // changed to be more narrow only if the assignment operator of the base
      // {Object} or {HeapObject} class was used, which we know to be safe.
      props.SetEffect(1, VisitExpr(call->getArg(1), env).Define(var_name));
    } else {
      VisitArguments<>(call, &props, env);
    }

    if (!props.IsSafe()) ReportUnsafe(call, BAD_EXPR_MSG);

    ExprEffect out = props.ComputeCumulativeEffect(
        RepresentsRawPointerType(call->getType()));

    clang::FunctionDecl* callee = call->getDirectCallee();
    if (callee == nullptr) return out;

    if (IsKnownToCauseGC(ctx_, callee)) {
      out.setGC();
      scopes_.back().SetGCCauseLocation(
          clang::FullSourceLoc(call->getExprLoc(), sm_), callee);
    }

    // Support for virtual methods that might be GC suspects.
    if (memcall == nullptr) return out;
    clang::CXXMethodDecl* method =
        llvm::dyn_cast_or_null<clang::CXXMethodDecl>(callee);
    if (method == nullptr) return out;
    if (!method->isVirtual()) return out;

    clang::CXXMethodDecl* target = method->getDevirtualizedMethod(
        memcall->getImplicitObjectArgument(), false);
    if (target != nullptr) {
      if (IsKnownToCauseGC(ctx_, target)) {
        out.setGC();
        scopes_.back().SetGCCauseLocation(
            clang::FullSourceLoc(call->getExprLoc(), sm_), target);
      }
    } else {
      // According to the documentation, {getDevirtualizedMethod} might
      // return nullptr, in which case we still want to use the partial
      // match of the {method}'s name against the GC suspects in order
      // to increase coverage.
      if (IsSuspectedToCauseGC(ctx_, method)) {
        out.setGC();
        scopes_.back().SetGCCauseLocation(
            clang::FullSourceLoc(call->getExprLoc(), sm_), method);
      }
    }
    return out;
  }

  // --------------------------------------------------------------------------
  // Statements
  // --------------------------------------------------------------------------

  Environment VisitStmt(clang::Stmt* stmt, const Environment& env) {
#define VISIT(type)                                                         \
  do {                                                                      \
    clang::type* concrete_stmt = llvm::dyn_cast_or_null<clang::type>(stmt); \
    if (concrete_stmt != nullptr) {                                         \
      return Visit##type(concrete_stmt, env);                               \
    }                                                                       \
  } while (0);

    if (clang::Expr* expr = llvm::dyn_cast_or_null<clang::Expr>(stmt)) {
      return env.ApplyEffect(VisitExpr(expr, env));
    }

    VISIT(AsmStmt);
    VISIT(BreakStmt);
    VISIT(CompoundStmt);
    VISIT(ContinueStmt);
    VISIT(CXXCatchStmt);
    VISIT(CXXTryStmt);
    VISIT(DeclStmt);
    VISIT(DoStmt);
    VISIT(ForStmt);
    VISIT(GotoStmt);
    VISIT(IfStmt);
    VISIT(IndirectGotoStmt);
    VISIT(LabelStmt);
    VISIT(NullStmt);
    VISIT(ReturnStmt);
    VISIT(CaseStmt);
    VISIT(DefaultStmt);
    VISIT(SwitchStmt);
    VISIT(WhileStmt);
#undef VISIT

    return env;
  }

#define DECL_VISIT_STMT(type) \
  Environment Visit##type(clang::type* stmt, const Environment& env)

#define IGNORE_STMT(type)                                              \
  Environment Visit##type(clang::type* stmt, const Environment& env) { \
    return env;                                                        \
  }

  IGNORE_STMT(IndirectGotoStmt);
  IGNORE_STMT(NullStmt);
  IGNORE_STMT(AsmStmt);

  // We are ignoring control flow for simplicity.
  IGNORE_STMT(GotoStmt);
  IGNORE_STMT(LabelStmt);

  // We are ignoring try/catch because V8 does not use them.
  IGNORE_STMT(CXXCatchStmt);
  IGNORE_STMT(CXXTryStmt);

  class Block {
   public:
    Block(const Environment& in, FunctionAnalyzer* owner)
        : in_(in),
          out_(Environment::Unreachable()),
          changed_(false),
          owner_(owner) {
      parent_ = owner_->EnterBlock(this);
    }

    ~Block() { owner_->LeaveBlock(parent_); }

    void MergeIn(const Environment& env) {
      Environment old_in = in_;
      in_ = Environment::Merge(in_, env);
      changed_ = !old_in.Equal(in_);
    }

    bool changed() {
      if (!changed_) return false;
      changed_ = false;
      return true;
    }

    const Environment& in() { return in_; }

    const Environment& out() { return out_; }

    void MergeOut(const Environment& env) {
      out_ = Environment::Merge(out_, env);
    }

    void Sequential(clang::Stmt* a, clang::Stmt* b, clang::Stmt* c) {
      Environment a_out = owner_->VisitStmt(a, in());
      Environment b_out = owner_->VisitStmt(b, a_out);
      Environment c_out = owner_->VisitStmt(c, b_out);
      MergeOut(c_out);
    }

    void Sequential(clang::Stmt* a, clang::Stmt* b) {
      Environment a_out = owner_->VisitStmt(a, in());
      Environment b_out = owner_->VisitStmt(b, a_out);
      MergeOut(b_out);
    }

    void Loop(clang::Stmt* a, clang::Stmt* b, clang::Stmt* c) {
      Sequential(a, b, c);
      MergeIn(out());
    }

    void Loop(clang::Stmt* a, clang::Stmt* b) {
      Sequential(a, b);
      MergeIn(out());
    }

   private:
    Environment in_;
    Environment out_;
    bool changed_;
    FunctionAnalyzer* owner_;
    Block* parent_;
  };

  DECL_VISIT_STMT(BreakStmt) {
    block_->MergeOut(env);
    return Environment::Unreachable();
  }

  DECL_VISIT_STMT(ContinueStmt) {
    block_->MergeIn(env);
    return Environment::Unreachable();
  }

  DECL_VISIT_STMT(CompoundStmt) {
    scopes_.push_back(GCScope());
    Environment out = env;
    clang::CompoundStmt::body_iterator end = stmt->body_end();
    for (clang::CompoundStmt::body_iterator s = stmt->body_begin(); s != end;
         ++s) {
      out = VisitStmt(*s, out);
    }
    scopes_.pop_back();
    return out;
  }

  DECL_VISIT_STMT(WhileStmt) {
    Block block(env, this);
    do {
      block.Loop(stmt->getCond(), stmt->getBody());
    } while (block.changed());
    return block.out();
  }

  DECL_VISIT_STMT(DoStmt) {
    Block block(env, this);

    // Special case `do { ... } while (false);`, which is known to only run
    // once, and is used in our (D)CHECK macros.
    if (auto* literal_cond =
            llvm::dyn_cast<clang::CXXBoolLiteralExpr>(stmt->getCond())) {
      if (literal_cond->getValue() == false) {
        block.Loop(stmt->getBody(), stmt->getCond());
        return block.out();
      }
    }

    do {
      block.Loop(stmt->getBody(), stmt->getCond());
    } while (block.changed());
    return block.out();
  }

  DECL_VISIT_STMT(ForStmt) {
    Block block(VisitStmt(stmt->getInit(), env), this);
    do {
      block.Loop(stmt->getCond(), stmt->getBody(), stmt->getInc());
    } while (block.changed());
    return block.out();
  }

  DECL_VISIT_STMT(IfStmt) {
    Environment cond_out = VisitStmt(stmt->getCond(), env);
    Environment then_out = VisitStmt(stmt->getThen(), cond_out);
    Environment else_out = VisitStmt(stmt->getElse(), cond_out);
    return Environment::Merge(then_out, else_out);
  }

  DECL_VISIT_STMT(SwitchStmt) {
    Block block(env, this);
    block.Sequential(stmt->getCond(), stmt->getBody());
    return block.out();
  }

  DECL_VISIT_STMT(CaseStmt) {
    Environment in = Environment::Merge(env, block_->in());
    Environment after_lhs = VisitStmt(stmt->getLHS(), in);
    return VisitStmt(stmt->getSubStmt(), after_lhs);
  }

  DECL_VISIT_STMT(DefaultStmt) {
    Environment in = Environment::Merge(env, block_->in());
    return VisitStmt(stmt->getSubStmt(), in);
  }

  DECL_VISIT_STMT(ReturnStmt) {
    VisitExpr(stmt->getRetValue(), env);
    return Environment::Unreachable();
  }

  const clang::TagType* ToTagType(const clang::Type* t) {
    if (t == nullptr) {
      return nullptr;
    } else if (llvm::isa<clang::TagType>(t)) {
      return llvm::cast<clang::TagType>(t);
    } else if (llvm::isa<clang::SubstTemplateTypeParmType>(t)) {
      return ToTagType(llvm::cast<clang::SubstTemplateTypeParmType>(t)
                           ->getReplacementType()
                           .getTypePtr());
    } else {
      return nullptr;
    }
  }

  bool IsDerivedFrom(const clang::CXXRecordDecl* record,
                     const clang::CXXRecordDecl* base) {
    return (record == base) || record->isDerivedFrom(base);
  }

  const clang::CXXRecordDecl* GetDefinitionOrNull(
      const clang::CXXRecordDecl* record) {
    if (record == nullptr) return nullptr;
    if (!InV8Namespace(record)) return nullptr;
    if (!record->hasDefinition()) return nullptr;
    return record->getDefinition();
  }

  bool IsDerivedFromInternalPointer(const clang::CXXRecordDecl* record) {
    if (record == nullptr) return false;
    if (!InV8Namespace(record)) return false;
    auto* specialization =
        llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
    if (specialization) {
      auto* template_decl =
          specialization->getSpecializedTemplate()->getCanonicalDecl();
      if (template_decl == tagged_decl_) {
        auto& template_args = specialization->getTemplateArgs();
        if (template_args.size() != 1) {
          llvm::errs() << "v8::internal::Tagged<T> should have exactly one "
                          "template argument\n";
          specialization->dump(llvm::errs());
          return false;
        }
        if (template_args[0].getKind() != clang::TemplateArgument::Type) {
          llvm::errs()
              << "v8::internal::Tagged<T>, T should be a type argument\n";
          specialization->dump(llvm::errs());
          return false;
        }

        auto* tagged_type_record =
            template_args[0].getAsType()->getAsCXXRecordDecl();
        return tagged_type_record != smi_decl_ &&
               tagged_type_record != tagged_index_decl_;
      }
    }

    const clang::CXXRecordDecl* definition = GetDefinitionOrNull(record);
    if (!definition) return false;
    if (IsDerivedFrom(record, heap_object_decl_)) {
      return true;
    }
    return false;
  }

  bool IsRawPointerType(const clang::PointerType* type) {
    const clang::CXXRecordDecl* record = type->getPointeeCXXRecordDecl();
    bool result = IsDerivedFromInternalPointer(record);
    TRACE("is raw " << result << " "
                    << (record ? record->getNameAsString() : "nullptr"));
    return result;
  }

  bool IsInternalPointerType(clang::QualType qtype) {
    const clang::CXXRecordDecl* record = qtype->getAsCXXRecordDecl();
    bool result = IsDerivedFromInternalPointer(record);
    TRACE_LLVM_TYPE("is internal " << result, qtype);
    return result;
  }

  // Returns weather the given type is a raw pointer or a wrapper around
  // such. For V8 that means Object and MaybeObject instances.
  bool RepresentsRawPointerType(clang::QualType qtype) {
    // Not yet assigned pointers can't get moved by the GC.
    if (qtype.isNull()) return false;
    // nullptr can't get moved by the GC.
    if (qtype->isNullPtrType()) return false;

    const clang::PointerType* pointer_type =
        llvm::dyn_cast_or_null<clang::PointerType>(qtype.getTypePtrOrNull());
    if (pointer_type != nullptr) {
      return IsRawPointerType(pointer_type);
    } else {
      return IsInternalPointerType(qtype);
    }
  }

  bool IsGCGuard(clang::QualType qtype) {
    if (!no_gc_mole_decl_) return false;
    if (qtype.isNull()) return false;
    if (qtype->isNullPtrType()) return false;

    const clang::CXXRecordDecl* record = qtype->getAsCXXRecordDecl();
    const clang::CXXRecordDecl* definition = GetDefinitionOrNull(record);

    if (!definition) return false;
    return no_gc_mole_decl_ == definition;
  }

  Environment VisitDecl(clang::Decl* decl, Environment& env) {
    if (clang::VarDecl* var = llvm::dyn_cast<clang::VarDecl>(decl)) {
      Environment out = var->hasInit() ? VisitStmt(var->getInit(), env) : env;

      if (RepresentsRawPointerType(var->getType())) {
        out = out.Define(var->getNameAsString());
      }
      if (IsGCGuard(var->getType())) {
        scopes_.back().guard_location =
            clang::FullSourceLoc(decl->getLocation(), sm_);
      }

      return out;
    }
    // TODO(gcmole): handle other declarations?
    return env;
  }

  DECL_VISIT_STMT(DeclStmt) {
    Environment out = env;
    clang::DeclStmt::decl_iterator end = stmt->decl_end();
    for (clang::DeclStmt::decl_iterator decl = stmt->decl_begin(); decl != end;
         ++decl) {
      out = VisitDecl(*decl, out);
    }
    return out;
  }

  void DefineParameters(const clang::FunctionDecl* f, Environment* env) {
    env->MDefine(THIS);
    clang::FunctionDecl::param_const_iterator end = f->param_end();
    for (clang::FunctionDecl::param_const_iterator p = f->param_begin();
         p != end; ++p) {
      env->MDefine((*p)->getNameAsString());
    }
  }

  void AnalyzeFunction(const clang::FunctionDecl* f) {
    const clang::FunctionDecl* body = nullptr;
    if (f->hasBody(body)) {
      Environment env;
      DefineParameters(body, &env);
      VisitStmt(body->getBody(), env);
      Environment::ClearSymbolTable();
    }
  }

  Block* EnterBlock(Block* block) {
    Block* parent = block_;
    block_ = block;
    return parent;
  }

  void LeaveBlock(Block* block) { block_ = block; }

  bool HasActiveGuard() {
    for (const auto& s : scopes_) {
      if (s.IsBeforeGCCause()) return true;
    }
    return false;
  }

 private:
  void ReportUnsafe(const clang::Expr* expr, const std::string& msg) {
    clang::SourceLocation error_loc =
        clang::FullSourceLoc(expr->getExprLoc(), sm_);
    d_.Report(error_loc,
              d_.getCustomDiagID(clang::DiagnosticsEngine::Warning, "%0"))
        << msg;
    // Find the relevant GC scope (see HasActiveGuard).
    const GCScope* pscope = nullptr;
    for (const auto& s : scopes_) {
      if (!s.IsBeforeGCCause() && s.gccause_location.isValid()) {
        pscope = &s;
        break;
      }
    }
    if (!pscope) {
      d_.Report(error_loc,
                d_.getCustomDiagID(clang::DiagnosticsEngine::Note,
                                   "Could not find GC source location."));
      return;
    }
    const GCScope& scope = *pscope;
    d_.Report(scope.gccause_location,
              d_.getCustomDiagID(clang::DiagnosticsEngine::Note,
                                 "Call might cause unexpected GC."));
    clang::FunctionDecl* gccause_decl = scope.gccause_decl;
    d_.Report(
        clang::FullSourceLoc(gccause_decl->getBeginLoc(), sm_),
        d_.getCustomDiagID(clang::DiagnosticsEngine::Note, "GC call here."));

    if (!g_print_gc_call_chain) return;
    // TODO(cbruni, v8::10009): print call-chain to gc with proper source
    // positions.
    LoadGCCauses();
    MangledName name;
    if (!GetMangledName(ctx_, gccause_decl, &name)) return;
    std::cout << "Potential GC call chain:\n";
    std::set<MangledName> stack;
    while (true) {
      if (!stack.insert(name).second) break;
      std::cout << "\t" << name << "\n";
      auto next = gc_causes.find(name);
      if (next == gc_causes.end()) break;
      std::vector<MangledName> calls = next->second;
      for (MangledName call : calls) {
        name = call;
        if (stack.find(call) != stack.end()) break;
      }
    }
  }

  clang::MangleContext* ctx_;
  clang::CXXRecordDecl* heap_object_decl_;
  clang::CXXRecordDecl* smi_decl_;
  clang::CXXRecordDecl* tagged_index_decl_;
  clang::ClassTemplateDecl* tagged_decl_;
  clang::CXXRecordDecl* no_gc_mole_decl_;

  clang::DiagnosticsEngine& d_;
  clang::SourceManager& sm_;

  Block* block_;

  struct GCScope {
    clang::FullSourceLoc guard_location;
    clang::FullSourceLoc gccause_location;
    clang::FunctionDecl* gccause_decl;

    // We're only interested in guards that are declared before any further GC
    // causing calls (see TestGuardedDeadVarAnalysisMidFunction for example).
    bool IsBeforeGCCause() const {
      if (!guard_location.isValid()) return false;
      if (!gccause_location.isValid()) return true;
      return guard_location.isBeforeInTranslationUnitThan(gccause_location);
    }

    // After we set the first GC cause in the scope, we don't need the later
    // ones.
    void SetGCCauseLocation(clang::FullSourceLoc gccause_location_,
                            clang::FunctionDecl* decl) {
      if (gccause_location.isValid()) return;
      gccause_location = gccause_location_;
      gccause_decl = decl;
    }
  };
  std::vector<GCScope> scopes_;
};

class ProblemsFinder : public clang::ASTConsumer,
                       public clang::RecursiveASTVisitor<ProblemsFinder> {
 public:
  ProblemsFinder(clang::DiagnosticsEngine& d, clang::SourceManager& sm,
                 const std::vector<std::string>& args)
      : d_(d), sm_(sm) {
    for (unsigned i = 0; i < args.size(); ++i) {
      if (args[i] == "--dead-vars") {
        g_dead_vars_analysis = true;
      }
      if (args[i] == "--verbose-trace") g_tracing_enabled = true;
      if (args[i] == "--verbose") g_verbose = true;
    }
  }

  bool TranslationUnitIgnored() {
    if (!ignored_files_loaded_) {
      std::ifstream fin("tools/gcmole/ignored_files");
      std::string s;
      while (fin >> s) ignored_files_.insert(s);
      ignored_files_loaded_ = true;
    }

    clang::FileID main_file_id = sm_.getMainFileID();
    std::string filename = sm_.getFileEntryForID(main_file_id)->getName().str();

    bool result = ignored_files_.find(filename) != ignored_files_.end();
    if (result) {
      llvm::outs() << "Ignoring file " << filename << "\n";
    }
    return result;
  }

  void HandleTranslationUnit(clang::ASTContext& ctx) override {
    if (TranslationUnitIgnored()) return;

    Resolver r(ctx);

    // It is a valid situation that no_gc_mole_decl == nullptr when
    // DisableGCMole is not included and can't be resolved. This is gracefully
    // handled in the FunctionAnalyzer later.
    auto v8_internal = r.ResolveNamespace("v8").ResolveNamespace("internal");
    clang::CXXRecordDecl* no_gc_mole_decl =
        v8_internal.Resolve<clang::CXXRecordDecl>("DisableGCMole");

    clang::CXXRecordDecl* heap_object_decl =
        v8_internal.Resolve<clang::CXXRecordDecl>("HeapObject");

    clang::CXXRecordDecl* smi_decl =
        v8_internal.Resolve<clang::CXXRecordDecl>("Smi");

    clang::CXXRecordDecl* tagged_index_decl =
        v8_internal.Resolve<clang::CXXRecordDecl>("TaggedIndex");

    clang::ClassTemplateDecl* tagged_decl =
        v8_internal.Resolve<clang::ClassTemplateDecl>("Tagged");

    if (heap_object_decl != nullptr) {
      heap_object_decl = heap_object_decl->getDefinition();
    }

    if (smi_decl != nullptr) {
      smi_decl = smi_decl->getDefinition();
    }

    if (tagged_index_decl != nullptr) {
      tagged_index_decl = tagged_index_decl->getDefinition();
    }

    if (tagged_decl != nullptr) {
      tagged_decl = tagged_decl->getCanonicalDecl();
    }

    if (heap_object_decl != nullptr && smi_decl != nullptr &&
        tagged_index_decl != nullptr && tagged_decl != nullptr) {
      function_analyzer_ = new FunctionAnalyzer(
          clang::ItaniumMangleContext::create(ctx, d_), heap_object_decl,
          smi_decl, tagged_index_decl, tagged_decl, no_gc_mole_decl, d_, sm_);
      TraverseDecl(ctx.getTranslationUnitDecl());
    } else if (g_verbose) {
      if (heap_object_decl == nullptr) {
        llvm::errs() << "Failed to resolve v8::internal::HeapObject\n";
      }
      if (smi_decl == nullptr) {
        llvm::errs() << "Failed to resolve v8::internal::Smi\n";
      }
      if (tagged_index_decl == nullptr) {
        llvm::errs() << "Failed to resolve v8::internal::TaggedIndex\n";
      }
      if (tagged_decl == nullptr) {
        llvm::errs() << "Failed to resolve v8::internal::Tagged<T>\n";
      }
    }
  }

  virtual bool VisitFunctionDecl(clang::FunctionDecl* decl) {
    // Don't print tracing from includes, otherwise the output is too big.
    bool tracing = g_tracing_enabled;
    const auto& fileID = sm_.getFileID(decl->getLocation());
    if (fileID != sm_.getMainFileID()) {
      g_tracing_enabled = false;
    }

    TRACE("Visiting function " << decl->getNameAsString());
    function_analyzer_->AnalyzeFunction(decl);

    g_tracing_enabled = tracing;
    return true;
  }

 private:
  clang::DiagnosticsEngine& d_;
  clang::SourceManager& sm_;

  bool ignored_files_loaded_ = false;
  std::set<std::string> ignored_files_;

  FunctionAnalyzer* function_analyzer_;
};

template <typename ConsumerType>
class Action : public clang::PluginASTAction {
 protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance& CI, llvm::StringRef InFile) override {
    return std::unique_ptr<clang::ASTConsumer>(
        new ConsumerType(CI.getDiagnostics(), CI.getSourceManager(), args_));
  }

  bool ParseArgs(const clang::CompilerInstance& CI,
                 const std::vector<std::string>& args) override {
    args_ = args;
    return true;
  }

  void PrintHelp(llvm::raw_ostream& ros) {}

 private:
  std::vector<std::string> args_;
};

}  // namespace

static clang::FrontendPluginRegistry::Add<Action<ProblemsFinder>> FindProblems(
    "find-problems", "Find GC-unsafe places.");

static clang::FrontendPluginRegistry::Add<Action<FunctionDeclarationFinder>>
    DumpCallees("dump-callees", "Dump callees for each function.");

#undef TRACE
#undef TRACE_LLVM_TYPE
#undef TRACE_LLVM_DECL
#undef DECL_VISIT_EXPR
#undef IGNORE_EXPR
#undef DECL_VISIT_STMT
#undef IGNORE_STMT

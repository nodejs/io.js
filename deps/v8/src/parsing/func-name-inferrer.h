// Copyright 2006-2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_FUNC_NAME_INFERRER_H_
#define V8_PARSING_FUNC_NAME_INFERRER_H_

#include <vector>

#include "src/base/macros.h"
#include "src/base/pointer-with-payload.h"
#include "src/base/small-vector.h"

namespace v8 {

namespace internal {
class AstRawString;
}

namespace base {
template <>
struct PointerWithPayloadTraits<v8::internal::AstRawString> {
  static constexpr int kAvailableBits = 2;
};
}  // namespace base

namespace internal {

class AstConsString;
class AstValueFactory;
class FunctionLiteral;

enum class InferName { kYes, kNo };

// FuncNameInferrer is a stateful class that is used to perform name
// inference for anonymous functions during static analysis of source code.
// Inference is performed in cases when an anonymous function is assigned
// to a variable or a property (see test-func-name-inference.cc for examples.)
//
// The basic idea is that during parsing of LHSs of certain expressions
// (assignments, declarations, object literals) we collect name strings,
// and during parsing of the RHS, a function literal can be collected. After
// parsing the RHS we can infer a name for function literals that do not have
// a name.
class FuncNameInferrer {
 public:
  explicit FuncNameInferrer(AstValueFactory* ast_value_factory);

  FuncNameInferrer(const FuncNameInferrer&) = delete;
  FuncNameInferrer& operator=(const FuncNameInferrer&) = delete;

  // To enter function name inference state, put a FuncNameInferrer::State
  // on the stack.
  class State {
   public:
    explicit State(FuncNameInferrer* fni)
        : fni_(fni), top_(fni->names_stack_.size()) {
      ++fni_->scope_depth_;
    }
    ~State() {
      DCHECK(fni_->IsOpen());
      fni_->names_stack_.resize_no_init(top_);
      --fni_->scope_depth_;
    }
    State(const State&) = delete;
    State& operator=(const State&) = delete;

   private:
    FuncNameInferrer* fni_;
    size_t top_;
  };

  // Returns whether we have entered name collection state.
  bool IsOpen() const { return scope_depth_ > 0; }

  // Pushes an enclosing the name of enclosing function onto names stack.
  void PushEnclosingName(const AstRawString* name);

  // Pushes an encountered name onto names stack when in collection state.
  void PushLiteralName(const AstRawString* name);

  void PushVariableName(const AstRawString* name);

  // Adds a function to infer name for.
  void AddFunction(FunctionLiteral* func_to_infer) {
    if (IsOpen()) {
      funcs_to_infer_.push_back(func_to_infer);
    }
  }

  void RemoveLastFunction() {
    if (IsOpen() && !funcs_to_infer_.empty()) funcs_to_infer_.pop_back();
  }

  void RemoveAsyncKeywordFromEnd();

  // Infers a function name and leaves names collection state.
  void Infer() {
    DCHECK(IsOpen());
    if (!funcs_to_infer_.empty()) InferFunctionsNames();
  }

 private:
  enum NameType : uint8_t {
    kEnclosingConstructorName,
    kLiteralName,
    kVariableName
  };
  struct Name {
    // Needed for names_stack_.resize()
    Name() { UNREACHABLE(); }
    Name(const AstRawString* name, NameType type)
        : name_and_type_(name, type) {}

    base::PointerWithPayload<const AstRawString, NameType, 2> name_and_type_;
    inline const AstRawString* name() const {
      return name_and_type_.GetPointer();
    }
    inline NameType type() const { return name_and_type_.GetPayload(); }
  };

  // Constructs a full name in dotted notation from gathered names.
  AstConsString* MakeNameFromStack();

  // Performs name inferring for added functions.
  void InferFunctionsNames();

  AstValueFactory* ast_value_factory_;
  base::SmallVector<Name, 8> names_stack_;
  std::vector<FunctionLiteral*> funcs_to_infer_;
  size_t scope_depth_ = 0;
};


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_FUNC_NAME_INFERRER_H_

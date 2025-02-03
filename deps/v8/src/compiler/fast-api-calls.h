// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_FAST_API_CALLS_H_
#define V8_COMPILER_FAST_API_CALLS_H_

#include "include/v8-fast-api-calls.h"
#include "src/compiler/graph-assembler.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace fast_api_call {

struct OverloadsResolutionResult {
  static OverloadsResolutionResult Invalid() {
    return OverloadsResolutionResult(-1, CTypeInfo::Type::kVoid);
  }

  OverloadsResolutionResult(int distinguishable_arg_index_,
                            CTypeInfo::Type element_type_)
      : distinguishable_arg_index(distinguishable_arg_index_),
        element_type(element_type_) {
    DCHECK(distinguishable_arg_index_ < 0 ||
           element_type_ != CTypeInfo::Type::kVoid);
  }

  bool is_valid() const { return distinguishable_arg_index >= 0; }

  // The index of the distinguishable overload argument. Only the case where the
  // types of this argument is a JSArray vs a TypedArray is supported.
  int distinguishable_arg_index;

  // The element type in the typed array argument.
  CTypeInfo::Type element_type;

  Node* target_address = nullptr;
};

ElementsKind GetTypedArrayElementsKind(CTypeInfo::Type type);

OverloadsResolutionResult ResolveOverloads(
    const FastApiCallFunctionVector& candidates, unsigned int arg_count);

bool CanOptimizeFastSignature(const CFunctionInfo* c_signature);

using GetParameter = std::function<Node*(int, OverloadsResolutionResult&,
                                         GraphAssemblerLabel<0>*)>;
using ConvertReturnValue = std::function<Node*(const CFunctionInfo*, Node*)>;
using InitializeOptions = std::function<void(Node*)>;
using GenerateSlowApiCall = std::function<Node*()>;

Node* BuildFastApiCall(Isolate* isolate, Graph* graph,
                       GraphAssembler* graph_assembler,
                       const FastApiCallFunctionVector& c_functions,
                       const CFunctionInfo* c_signature, Node* data_argument,
                       const GetParameter& get_parameter,
                       const ConvertReturnValue& convert_return_value,
                       const InitializeOptions& initialize_options,
                       const GenerateSlowApiCall& generate_slow_api_call);

}  // namespace fast_api_call
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_FAST_API_CALLS_H_

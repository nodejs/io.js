// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_CONTEXT_SPECIALIZATION_H_
#define V8_COMPILER_JS_CONTEXT_SPECIALIZATION_H_

#include "src/compiler/graph-reducer.h"
#include "src/handles/maybe-handles.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class JSGraph;
class JSOperatorBuilder;

// Pair of a context and its distance from some point of reference.
struct OuterContext {
  OuterContext() = default;
  OuterContext(Handle<Context> context_, size_t distance_)
      : context(context_), distance(distance_) {}

  Handle<Context> context;
  size_t distance = 0;
};

// Specializes a given JSGraph to a given context, potentially constant folding
// some {LoadContext} nodes or strength reducing some {StoreContext} nodes.
// Additionally, constant-folds the function parameter if {closure} is given,
// and constant-folds import.meta loads if the corresponding object already
// exists.
//
// The context can be the incoming function context or any outer context
// thereof, as indicated by {outer}'s {distance}.
class V8_EXPORT_PRIVATE JSContextSpecialization final : public AdvancedReducer {
 public:
  JSContextSpecialization(Editor* editor, JSGraph* jsgraph,
                          JSHeapBroker* broker, Maybe<OuterContext> outer,
                          MaybeHandle<JSFunction> closure)
      : AdvancedReducer(editor),
        jsgraph_(jsgraph),
        outer_(outer),
        closure_(closure),
        broker_(broker) {}
  JSContextSpecialization(const JSContextSpecialization&) = delete;
  JSContextSpecialization& operator=(const JSContextSpecialization&) = delete;

  const char* reducer_name() const override {
    return "JSContextSpecialization";
  }

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceParameter(Node* node);
  Reduction ReduceJSLoadContext(Node* node);
  Reduction ReduceJSStoreContext(Node* node);
  Reduction ReduceJSStoreScriptContext(Node* node);
  Reduction ReduceJSGetImportMeta(Node* node);

  Reduction SimplifyJSStoreContext(Node* node, Node* new_context,
                                   size_t new_depth);
  Reduction SimplifyJSLoadContext(Node* node, Node* new_context,
                                  size_t new_depth);

  Isolate* isolate() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Maybe<OuterContext> outer() const { return outer_; }
  MaybeHandle<JSFunction> closure() const { return closure_; }
  JSHeapBroker* broker() const { return broker_; }

  JSGraph* const jsgraph_;
  Maybe<OuterContext> outer_;
  MaybeHandle<JSFunction> closure_;
  JSHeapBroker* const broker_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_CONTEXT_SPECIALIZATION_H_

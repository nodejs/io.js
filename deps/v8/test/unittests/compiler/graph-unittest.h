// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_COMPILER_GRAPH_UNITTEST_H_
#define V8_UNITTESTS_COMPILER_GRAPH_UNITTEST_H_

#include "src/codegen/tick-counter.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/typer.h"
#include "src/handles/handles.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace v8 {
namespace internal {

// Forward declarations.
class HeapObject;

namespace compiler {

using ::testing::Matcher;

class GraphTest : public TestWithNativeContextAndZone {
 public:
  explicit GraphTest(int num_parameters = 1);
  ~GraphTest() override = default;

  void Reset();

  Node* start() { return graph()->start(); }
  Node* end() { return graph()->end(); }

  Node* Parameter(int32_t index = 0);
  Node* Parameter(Type type, int32_t index = 0);
  Node* Float32Constant(float value);
  Node* Float64Constant(double value);
  Node* Int32Constant(int32_t value);
  Node* Uint32Constant(uint32_t value) {
    return Int32Constant(base::bit_cast<int32_t>(value));
  }
  Node* Int64Constant(int64_t value);
  Node* Uint64Constant(uint64_t value) {
    return Int64Constant(base::bit_cast<int64_t>(value));
  }
  Node* NumberConstant(double value);
  Node* HeapConstantNoHole(const Handle<HeapObject>& value);
  Node* HeapConstantHole(const Handle<HeapObject>& value);
  Node* FalseConstant();
  Node* TrueConstant();
  Node* UndefinedConstant();

  Node* EmptyFrameState();

  Matcher<Node*> IsBooleanConstant(bool value) {
    return value ? IsTrueConstant() : IsFalseConstant();
  }
  Matcher<Node*> IsFalseConstant();
  Matcher<Node*> IsTrueConstant();
  Matcher<Node*> IsNullConstant();
  Matcher<Node*> IsUndefinedConstant();

  CommonOperatorBuilder* common() { return &data_->common_; }
  Graph* graph() { return &data_->graph_; }
  SourcePositionTable* source_positions() { return &data_->source_positions_; }
  NodeOriginTable* node_origins() { return &data_->node_origins_; }
  JSHeapBroker* broker() { return &data_->broker_; }
  template <typename T>
  Handle<T> CanonicalHandle(T object) {
    static_assert(kTaggedCanConvertToRawObjects);
    return CanonicalHandle(Tagged<T>(object));
  }
  template <typename T>
  Handle<T> CanonicalHandle(Tagged<T> object) {
    return broker()->CanonicalPersistentHandle(object);
  }
  template <typename T>
  Handle<T> CanonicalHandle(Handle<T> handle) {
    return CanonicalHandle(*handle);
  }
  TickCounter* tick_counter() { return &data_->tick_counter_; }

 private:
  struct Data {
    Data(Isolate* isolate, Zone* zone, int num_parameters);
    ~Data();
    CommonOperatorBuilder common_;
    Graph graph_;
    JSHeapBroker broker_;
    JSHeapBrokerScopeForTesting broker_scope_;
    std::unique_ptr<PersistentHandlesScope> persistent_scope_;
    CurrentHeapBrokerScope current_broker_;
    SourcePositionTable source_positions_;
    NodeOriginTable node_origins_;
    TickCounter tick_counter_;
    int num_parameters_;
  };
  std::unique_ptr<Data> data_;
};


class TypedGraphTest : public GraphTest {
 public:
  explicit TypedGraphTest(int num_parameters = 1);
  ~TypedGraphTest() override;

 protected:
  Typer* typer() { return &typer_; }

 private:
  Typer typer_;
};

}  //  namespace compiler
}  //  namespace internal
}  //  namespace v8

#endif  // V8_UNITTESTS_COMPILER_GRAPH_UNITTEST_H_

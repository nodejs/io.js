// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PROPERTY_ACCESS_BUILDER_H_
#define V8_COMPILER_PROPERTY_ACCESS_BUILDER_H_

#include "src/base/optional.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node.h"
#include "src/handles/handles.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class CommonOperatorBuilder;
class CompilationDependencies;
class Graph;
class JSGraph;
class JSHeapBroker;
class PropertyAccessInfo;
class SimplifiedOperatorBuilder;
struct FieldAccess;

class PropertyAccessBuilder {
 public:
  PropertyAccessBuilder(JSGraph* jsgraph, JSHeapBroker* broker)
      : jsgraph_(jsgraph), broker_(broker) {}

  // Builds the appropriate string check if the maps are only string
  // maps.
  bool TryBuildStringCheck(JSHeapBroker* broker, ZoneVector<MapRef> const& maps,
                           Node** receiver, Effect* effect, Control control);
  // Builds a number check if all maps are number maps.
  bool TryBuildNumberCheck(JSHeapBroker* broker, ZoneVector<MapRef> const& maps,
                           Node** receiver, Effect* effect, Control control);

  void BuildCheckMaps(Node* object, Effect* effect, Control control,
                      ZoneVector<MapRef> const& maps);

  Node* BuildCheckValue(Node* receiver, Effect* effect, Control control,
                        Handle<HeapObject> value);

  // Builds the actual load for data-field and data-constant-field
  // properties (without heap-object or map checks).
  Node* BuildLoadDataField(NameRef name, PropertyAccessInfo const& access_info,
                           Node* lookup_start_object, Node** effect,
                           Node** control);

  // Tries to load a constant value from a prototype object in dictionary mode
  // and constant-folds it. Returns {} if the constant couldn't be safely
  // retrieved.
  base::Optional<Node*> FoldLoadDictPrototypeConstant(
      PropertyAccessInfo const& access_info);

  static MachineRepresentation ConvertRepresentation(
      Representation representation);

 private:
  JSGraph* jsgraph() const { return jsgraph_; }
  JSHeapBroker* broker() const { return broker_; }
  CompilationDependencies* dependencies() const {
    return broker_->dependencies();
  }
  Graph* graph() const;
  Isolate* isolate() const;
  CommonOperatorBuilder* common() const;
  SimplifiedOperatorBuilder* simplified() const;

  Node* TryFoldLoadConstantDataField(NameRef name,
                                     PropertyAccessInfo const& access_info,
                                     Node* lookup_start_object);
  // Returns a node with the holder for the property access described by
  // {access_info}.
  Node* ResolveHolder(PropertyAccessInfo const& access_info,
                      Node* lookup_start_object);

  Node* BuildLoadDataField(NameRef name, Node* holder,
                           FieldAccess&& field_access, bool is_inobject,
                           Node** effect, Node** control);

  JSGraph* jsgraph_;
  JSHeapBroker* broker_;
};

bool HasOnlyStringMaps(JSHeapBroker* broker, ZoneVector<MapRef> const& maps);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PROPERTY_ACCESS_BUILDER_H_

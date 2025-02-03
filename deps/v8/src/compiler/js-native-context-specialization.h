// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_NATIVE_CONTEXT_SPECIALIZATION_H_
#define V8_COMPILER_JS_NATIVE_CONTEXT_SPECIALIZATION_H_

#include <optional>

#include "src/base/flags.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-heap-broker.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Factory;
class JSGlobalObject;
class JSGlobalProxy;

namespace compiler {

// Forward declarations.
enum class AccessMode;
class CommonOperatorBuilder;
class CompilationDependencies;
class ElementAccessInfo;
class JSGraph;
class JSHeapBroker;
class JSOperatorBuilder;
class MachineOperatorBuilder;
class PropertyAccessInfo;
class SimplifiedOperatorBuilder;
class TypeCache;

// Specializes a given JSGraph to a given native context, potentially constant
// folding some {LoadGlobal} nodes or strength reducing some {StoreGlobal}
// nodes.  And also specializes {LoadNamed} and {SetNamedProperty} nodes
// according to type feedback (if available).
class V8_EXPORT_PRIVATE JSNativeContextSpecialization final
    : public AdvancedReducer {
 public:
  // Flags that control the mode of operation.
  enum Flag {
    kNoFlags = 0u,
    kBailoutOnUninitialized = 1u << 0,
  };
  using Flags = base::Flags<Flag>;

  JSNativeContextSpecialization(Editor* editor, JSGraph* jsgraph,
                                JSHeapBroker* broker, Flags flags, Zone* zone,
                                Zone* shared_zone);
  JSNativeContextSpecialization(const JSNativeContextSpecialization&) = delete;
  JSNativeContextSpecialization& operator=(
      const JSNativeContextSpecialization&) = delete;

  const char* reducer_name() const override {
    return "JSNativeContextSpecialization";
  }

  Reduction Reduce(Node* node) final;

  // Utility for folding string constant concatenation.
  // Supports JSAdd nodes and nodes typed as string or number.
  // Public for the sake of unit testing.
  static std::optional<size_t> GetMaxStringLength(JSHeapBroker* broker,
                                                  Node* node);

 private:
  Reduction ReduceJSAdd(Node* node);
  Reduction ReduceJSAsyncFunctionEnter(Node* node);
  Reduction ReduceJSAsyncFunctionReject(Node* node);
  Reduction ReduceJSAsyncFunctionResolve(Node* node);
  Reduction ReduceJSGetSuperConstructor(Node* node);
  Reduction ReduceJSFindNonDefaultConstructorOrConstruct(Node* node);
  Reduction ReduceJSInstanceOf(Node* node);
  Reduction ReduceJSHasInPrototypeChain(Node* node);
  Reduction ReduceJSOrdinaryHasInstance(Node* node);
  Reduction ReduceJSPromiseResolve(Node* node);
  Reduction ReduceJSResolvePromise(Node* node);
  Reduction ReduceJSLoadGlobal(Node* node);
  Reduction ReduceJSStoreGlobal(Node* node);
  Reduction ReduceJSLoadNamed(Node* node);
  Reduction ReduceJSLoadNamedFromSuper(Node* node);
  Reduction ReduceJSGetIterator(Node* node);
  Reduction ReduceJSSetNamedProperty(Node* node);
  Reduction ReduceJSHasProperty(Node* node);
  Reduction ReduceJSLoadProperty(Node* node);
  Reduction ReduceJSSetKeyedProperty(Node* node);
  Reduction ReduceJSDefineKeyedOwnProperty(Node* node);
  Reduction ReduceJSDefineNamedOwnProperty(Node* node);
  Reduction ReduceJSDefineKeyedOwnPropertyInLiteral(Node* node);
  Reduction ReduceJSStoreInArrayLiteral(Node* node);
  Reduction ReduceJSToObject(Node* node);

  Reduction ReduceElementAccess(Node* node, Node* index, Node* value,
                                ElementAccessFeedback const& feedback);
  // In the case of non-keyed (named) accesses, pass the name as {static_name}
  // and use {nullptr} for {key} (load/store modes are irrelevant).
  Reduction ReducePropertyAccess(Node* node, Node* key,
                                 OptionalNameRef static_name, Node* value,
                                 FeedbackSource const& source,
                                 AccessMode access_mode);
  Reduction ReduceNamedAccess(Node* node, Node* value,
                              NamedAccessFeedback const& feedback,
                              AccessMode access_mode, Node* key = nullptr);
  Reduction ReduceMegaDOMPropertyAccess(
      Node* node, Node* value, MegaDOMPropertyAccessFeedback const& feedback,
      FeedbackSource const& source);
  Reduction ReduceGlobalAccess(Node* node, Node* lookup_start_object,
                               Node* receiver, Node* value, NameRef name,
                               AccessMode access_mode, Node* key,
                               PropertyCellRef property_cell,
                               Node* effect = nullptr);
  Reduction ReduceElementLoadFromHeapConstant(Node* node, Node* key,
                                              AccessMode access_mode,
                                              KeyedAccessLoadMode load_mode);
  Reduction ReduceElementAccessOnString(Node* node, Node* index, Node* value,
                                        KeyedAccessMode const& keyed_mode);

  Reduction ReduceEagerDeoptimize(Node* node, DeoptimizeReason reason);
  Reduction ReduceJSToString(Node* node);

  Reduction ReduceJSLoadPropertyWithEnumeratedKey(Node* node);

  Handle<String> CreateStringConstant(Node* node);

  // A triple of nodes that represents a continuation.
  class ValueEffectControl final {
   public:
    ValueEffectControl()
        : value_(nullptr), effect_(nullptr), control_(nullptr) {}
    ValueEffectControl(Node* value, Node* effect, Node* control)
        : value_(value), effect_(effect), control_(control) {}

    Node* value() const { return value_; }
    Node* effect() const { return effect_; }
    Node* control() const { return control_; }

   private:
    Node* value_;
    Node* effect_;
    Node* control_;
  };

  // Construct the appropriate subgraph for property access. Return {} if the
  // property access couldn't be built.
  std::optional<ValueEffectControl> BuildPropertyAccess(
      Node* lookup_start_object, Node* receiver, Node* value, Node* context,
      Node* frame_state, Node* effect, Node* control, NameRef name,
      ZoneVector<Node*>* if_exceptions, PropertyAccessInfo const& access_info,
      AccessMode access_mode);
  std::optional<ValueEffectControl> BuildPropertyLoad(
      Node* lookup_start_object, Node* receiver, Node* context,
      Node* frame_state, Node* effect, Node* control, NameRef name,
      ZoneVector<Node*>* if_exceptions, PropertyAccessInfo const& access_info);

  ValueEffectControl BuildPropertyStore(Node* receiver, Node* value,
                                        Node* context, Node* frame_state,
                                        Node* effect, Node* control,
                                        NameRef name,
                                        ZoneVector<Node*>* if_exceptions,
                                        PropertyAccessInfo const& access_info,
                                        AccessMode access_mode);

  ValueEffectControl BuildPropertyTest(Node* effect, Node* control,
                                       PropertyAccessInfo const& access_info);

  // Helpers for accessor inlining.
  Node* InlinePropertyGetterCall(Node* receiver,
                                 ConvertReceiverMode receiver_mode,
                                 Node* lookup_start_object, Node* context,
                                 Node* frame_state, Node** effect,
                                 Node** control,
                                 ZoneVector<Node*>* if_exceptions,
                                 PropertyAccessInfo const& access_info);
  void InlinePropertySetterCall(Node* receiver, Node* value, Node* context,
                                Node* frame_state, Node** effect,
                                Node** control,
                                ZoneVector<Node*>* if_exceptions,
                                PropertyAccessInfo const& access_info);
  Node* InlineApiCall(Node* receiver, Node* api_holder, Node* frame_state,
                      Node* value, Node** effect, Node** control,
                      FunctionTemplateInfoRef function_template_info);

  // Construct the appropriate subgraph for element access.
  ValueEffectControl BuildElementAccess(Node* receiver, Node* index,
                                        Node* value, Node* effect,
                                        Node* control, Node* context,
                                        ElementAccessInfo const& access_info,
                                        KeyedAccessMode const& keyed_mode);
  ValueEffectControl BuildElementAccessForTypedArrayOrRabGsabTypedArray(
      Node* receiver, Node* index, Node* value, Node* effect, Node* control,
      Node* context, ElementsKind elements_kind,
      KeyedAccessMode const& keyed_mode);

  // Construct appropriate subgraph to load from a String.
  Node* BuildIndexedStringLoad(Node* receiver, Node* index, Node* length,
                               Node** effect, Node** control,
                               KeyedAccessLoadMode load_mode);

  // Construct appropriate subgraph to extend properties backing store.
  Node* BuildExtendPropertiesBackingStore(MapRef map, Node* properties,
                                          Node* effect, Node* control);

  // Construct appropriate subgraph to check that the {value} matches
  // the previously recorded {name} feedback.
  Node* BuildCheckEqualsName(NameRef name, Node* value, Node* effect,
                             Node* control);

  // Concatenates {left} and {right}.
  Handle<String> Concatenate(Handle<String> left, Handle<String> right);

  // Returns true if {str} can safely be read:
  //   - if we are on the main thread, then any string can safely be read
  //   - in the background, we can only read some string shapes, except if we
  //     created the string ourselves.
  // {node} is the node from which we got {str}, but which is still taken as
  // parameter to simplify the checks.
  bool StringCanSafelyBeRead(Node* const node, Handle<String> str);

  // Checks if we can turn the hole into undefined when loading an element
  // from an object with one of the {receiver_maps}; sets up appropriate
  // code dependencies and might use the array protector cell.
  bool CanTreatHoleAsUndefined(ZoneVector<MapRef> const& receiver_maps);

  void RemoveImpossibleMaps(Node* object, ZoneVector<MapRef>* maps) const;

  ElementAccessFeedback const& TryRefineElementAccessFeedback(
      ElementAccessFeedback const& feedback, Node* receiver,
      Effect effect) const;

  // Try to infer maps for the given {object} at the current {effect}.
  bool InferMaps(Node* object, Effect effect, ZoneVector<MapRef>* maps) const;

  // Try to infer a root map for the {object} independent of the current program
  // location.
  OptionalMapRef InferRootMap(Node* object) const;

  // Checks if we know at compile time that the {receiver} either definitely
  // has the {prototype} in it's prototype chain, or the {receiver} definitely
  // doesn't have the {prototype} in it's prototype chain.
  enum InferHasInPrototypeChainResult {
    kIsInPrototypeChain,
    kIsNotInPrototypeChain,
    kMayBeInPrototypeChain
  };
  InferHasInPrototypeChainResult InferHasInPrototypeChain(
      Node* receiver, Effect effect, HeapObjectRef prototype);

  Node* BuildLoadPrototypeFromObject(Node* object, Node* effect, Node* control);

  std::pair<Node*, Node*> ReleaseEffectAndControlFromAssembler(
      JSGraphAssembler* assembler);

  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }

  JSHeapBroker* broker() const { return broker_; }
  Isolate* isolate() const;
  Factory* factory() const;
  CommonOperatorBuilder* common() const;
  JSOperatorBuilder* javascript() const;
  SimplifiedOperatorBuilder* simplified() const;
  Flags flags() const { return flags_; }
  Handle<JSGlobalObject> global_object() const { return global_object_; }
  Handle<JSGlobalProxy> global_proxy() const { return global_proxy_; }
  NativeContextRef native_context() const {
    return broker()->target_native_context();
  }
  CompilationDependencies* dependencies() const {
    return broker()->dependencies();
  }
  Zone* zone() const { return zone_; }
  Zone* shared_zone() const { return shared_zone_; }

  JSGraph* const jsgraph_;
  JSHeapBroker* const broker_;
  Flags const flags_;
  Handle<JSGlobalObject> global_object_;
  Handle<JSGlobalProxy> global_proxy_;
  Zone* const zone_;
  Zone* const shared_zone_;
  TypeCache const* type_cache_;
  ZoneUnorderedSet<Handle<String>, Handle<String>::hash,
                   Handle<String>::equal_to>
      created_strings_;
};

DEFINE_OPERATORS_FOR_FLAGS(JSNativeContextSpecialization::Flags)

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_NATIVE_CONTEXT_SPECIALIZATION_H_

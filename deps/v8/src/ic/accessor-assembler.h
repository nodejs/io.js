// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_ACCESSOR_ASSEMBLER_H_
#define V8_IC_ACCESSOR_ASSEMBLER_H_

#include <optional>

#include "src/codegen/code-stub-assembler.h"
#include "src/compiler/code-assembler.h"
#include "src/objects/dictionary.h"

namespace v8 {
namespace internal {

namespace compiler {
class CodeAssemblerState;
}  // namespace compiler

class ExitPoint;

class V8_EXPORT_PRIVATE AccessorAssembler : public CodeStubAssembler {
 public:
  explicit AccessorAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  void GenerateLoadIC();
  void GenerateLoadIC_Megamorphic();
  void GenerateLoadIC_Noninlined();
  void GenerateLoadIC_NoFeedback();
  void GenerateLoadGlobalIC_NoFeedback();
  void GenerateLoadICTrampoline();
  void GenerateLoadICBaseline();
  void GenerateLoadICTrampoline_Megamorphic();
  void GenerateLoadSuperIC();
  void GenerateLoadSuperICBaseline();
  void GenerateKeyedLoadIC();
  void GenerateEnumeratedKeyedLoadIC();
  void GenerateKeyedLoadIC_Megamorphic();
  void GenerateKeyedLoadIC_PolymorphicName();
  void GenerateKeyedLoadICTrampoline();
  void GenerateKeyedLoadICBaseline();
  void GenerateEnumeratedKeyedLoadICBaseline();
  void GenerateKeyedLoadICTrampoline_Megamorphic();
  void GenerateStoreIC();
  void GenerateStoreIC_Megamorphic();
  void GenerateStoreICTrampoline();
  void GenerateStoreICTrampoline_Megamorphic();
  void GenerateStoreICBaseline();
  void GenerateDefineNamedOwnIC();
  void GenerateDefineNamedOwnICTrampoline();
  void GenerateDefineNamedOwnICBaseline();
  void GenerateStoreGlobalIC();
  void GenerateStoreGlobalICTrampoline();
  void GenerateStoreGlobalICBaseline();
  void GenerateCloneObjectIC();
  void GenerateCloneObjectICBaseline();
  void GenerateCloneObjectIC_Slow();
  void GenerateKeyedHasIC();
  void GenerateKeyedHasICBaseline();
  void GenerateKeyedHasIC_Megamorphic();
  void GenerateKeyedHasIC_PolymorphicName();

  void GenerateLoadGlobalIC(TypeofMode typeof_mode);
  void GenerateLoadGlobalICTrampoline(TypeofMode typeof_mode);
  void GenerateLoadGlobalICBaseline(TypeofMode typeof_mode);
  void GenerateLookupGlobalIC(TypeofMode typeof_mode);
  void GenerateLookupGlobalICTrampoline(TypeofMode typeof_mode);
  void GenerateLookupGlobalICBaseline(TypeofMode typeof_mode);
  void GenerateLookupContextTrampoline(TypeofMode typeof_mode,
                                       ContextKind context_kind);
  void GenerateLookupContextBaseline(TypeofMode typeof_mode,
                                     ContextKind context_kind);

  void GenerateKeyedStoreIC();
  void GenerateKeyedStoreICTrampoline();
  void GenerateKeyedStoreICTrampoline_Megamorphic();
  void GenerateKeyedStoreICBaseline();

  void GenerateDefineKeyedOwnIC();
  void GenerateDefineKeyedOwnICTrampoline();
  void GenerateDefineKeyedOwnICBaseline();

  void GenerateStoreInArrayLiteralIC();
  void GenerateStoreInArrayLiteralICBaseline();

  void TryProbeStubCache(StubCache* stub_cache,
                         TNode<Object> lookup_start_object,
                         TNode<Map> lookup_start_object_map, TNode<Name> name,
                         Label* if_handler, TVariable<MaybeObject>* var_handler,
                         Label* if_miss);
  void TryProbeStubCache(StubCache* stub_cache,
                         TNode<Object> lookup_start_object, TNode<Name> name,
                         Label* if_handler, TVariable<MaybeObject>* var_handler,
                         Label* if_miss) {
    return TryProbeStubCache(stub_cache, lookup_start_object,
                             LoadReceiverMap(lookup_start_object), name,
                             if_handler, var_handler, if_miss);
  }

  TNode<IntPtrT> StubCachePrimaryOffsetForTesting(TNode<Name> name,
                                                  TNode<Map> map) {
    return StubCachePrimaryOffset(name, map);
  }
  TNode<IntPtrT> StubCacheSecondaryOffsetForTesting(TNode<Name> name,
                                                    TNode<Map> map) {
    return StubCacheSecondaryOffset(name, map);
  }

  struct LoadICParameters {
    LoadICParameters(
        TNode<Context> context, TNode<Object> receiver, TNode<Object> name,
        TNode<TaggedIndex> slot, TNode<HeapObject> vector,
        std::optional<TNode<Object>> lookup_start_object = std::nullopt,
        std::optional<TNode<Smi>> enum_index = std::nullopt,
        std::optional<TNode<Object>> cache_type = std::nullopt)
        : context_(context),
          receiver_(receiver),
          name_(name),
          slot_(slot),
          vector_(vector),
          lookup_start_object_(lookup_start_object ? lookup_start_object.value()
                                                   : receiver),
          enum_index_(enum_index),
          cache_type_(cache_type) {}

    LoadICParameters(const LoadICParameters* p, TNode<Object> unique_name)
        : context_(p->context_),
          receiver_(p->receiver_),
          name_(unique_name),
          slot_(p->slot_),
          vector_(p->vector_),
          lookup_start_object_(p->lookup_start_object_) {}

    TNode<Context> context() const { return context_; }
    TNode<Object> receiver() const { return receiver_; }
    TNode<Object> name() const { return name_; }
    TNode<TaggedIndex> slot() const { return slot_; }
    TNode<HeapObject> vector() const { return vector_; }
    TNode<Object> lookup_start_object() const {
      return lookup_start_object_.value();
    }
    TNode<Smi> enum_index() const { return *enum_index_; }
    TNode<Object> cache_type() const { return *cache_type_; }

    // Usable in cases where the receiver and the lookup start object are
    // expected to be the same, i.e., when "receiver != lookup_start_object"
    // case is not supported or not expected by the surrounding code.
    TNode<Object> receiver_and_lookup_start_object() const {
      DCHECK_EQ(receiver_, lookup_start_object_);
      return receiver_;
    }

    bool IsEnumeratedKeyedLoad() const { return enum_index_ != std::nullopt; }

   private:
    TNode<Context> context_;
    TNode<Object> receiver_;
    TNode<Object> name_;
    TNode<TaggedIndex> slot_;
    TNode<HeapObject> vector_;
    std::optional<TNode<Object>> lookup_start_object_;
    std::optional<TNode<Smi>> enum_index_;
    std::optional<TNode<Object>> cache_type_;
  };

  struct LazyLoadICParameters {
    LazyLoadICParameters(
        LazyNode<Context> context, TNode<Object> receiver,
        LazyNode<Object> name, LazyNode<TaggedIndex> slot,
        TNode<HeapObject> vector,
        std::optional<TNode<Object>> lookup_start_object = std::nullopt)
        : context_(context),
          receiver_(receiver),
          name_(name),
          slot_(slot),
          vector_(vector),
          lookup_start_object_(lookup_start_object ? lookup_start_object.value()
                                                   : receiver) {}

    explicit LazyLoadICParameters(const LoadICParameters* p)
        : receiver_(p->receiver()),
          vector_(p->vector()),
          lookup_start_object_(p->lookup_start_object()) {
      slot_ = [=] { return p->slot(); };
      context_ = [=] { return p->context(); };
      name_ = [=] { return p->name(); };
    }

    TNode<Context> context() const { return context_(); }
    TNode<Object> receiver() const { return receiver_; }
    TNode<Object> name() const { return name_(); }
    TNode<TaggedIndex> slot() const { return slot_(); }
    TNode<HeapObject> vector() const { return vector_; }
    TNode<Object> lookup_start_object() const { return lookup_start_object_; }

    // Usable in cases where the receiver and the lookup start object are
    // expected to be the same, i.e., when "receiver != lookup_start_object"
    // case is not supported or not expected by the surrounding code.
    TNode<Object> receiver_and_lookup_start_object() const {
      DCHECK_EQ(receiver_, lookup_start_object_);
      return receiver_;
    }

   private:
    LazyNode<Context> context_;
    TNode<Object> receiver_;
    LazyNode<Object> name_;
    LazyNode<TaggedIndex> slot_;
    TNode<HeapObject> vector_;
    TNode<Object> lookup_start_object_;
  };

  void LoadGlobalIC(TNode<HeapObject> maybe_feedback_vector,
                    const LazyNode<TaggedIndex>& lazy_slot,
                    const LazyNode<Context>& lazy_context,
                    const LazyNode<Name>& lazy_name, TypeofMode typeof_mode,
                    ExitPoint* exit_point);

  // Specialized LoadIC for inlined bytecode handler, hand-tuned to omit frame
  // construction on common paths.
  void LoadIC_BytecodeHandler(const LazyLoadICParameters* p,
                              ExitPoint* exit_point);

  // Loads dataX field from the DataHandler object.
  TNode<MaybeObject> LoadHandlerDataField(TNode<DataHandler> handler,
                                          int data_index);

 protected:
  enum class StoreICMode {
    // TODO(v8:12548): rename to kDefineKeyedOwnInLiteral
    kDefault,
    kDefineNamedOwn,
    kDefineKeyedOwn,
  };
  struct StoreICParameters {
    StoreICParameters(TNode<Context> context,
                      std::optional<TNode<Object>> receiver, TNode<Object> name,
                      TNode<Object> value, std::optional<TNode<Smi>> flags,
                      TNode<TaggedIndex> slot, TNode<HeapObject> vector,
                      StoreICMode mode)
        : context_(context),
          receiver_(receiver),
          name_(name),
          value_(value),
          flags_(flags),
          slot_(slot),
          vector_(vector),
          mode_(mode) {}

    TNode<Context> context() const { return context_; }
    TNode<Object> receiver() const { return receiver_.value(); }
    TNode<Object> name() const { return name_; }
    TNode<Object> value() const { return value_; }
    TNode<Smi> flags() const { return flags_.value(); }
    TNode<TaggedIndex> slot() const { return slot_; }
    TNode<HeapObject> vector() const { return vector_; }

    TNode<Object> lookup_start_object() const { return receiver(); }

    bool receiver_is_null() const { return !receiver_.has_value(); }
    bool flags_is_null() const { return !flags_.has_value(); }

    bool IsDefineNamedOwn() const {
      return mode_ == StoreICMode::kDefineNamedOwn;
    }
    bool IsDefineKeyedOwn() const {
      return mode_ == StoreICMode::kDefineKeyedOwn;
    }
    bool IsAnyDefineOwn() const {
      return IsDefineNamedOwn() || IsDefineKeyedOwn();
    }

    StubCache* stub_cache(Isolate* isolate) const {
      return IsAnyDefineOwn() ? isolate->define_own_stub_cache()
                              : isolate->store_stub_cache();
    }

   private:
    TNode<Context> context_;
    std::optional<TNode<Object>> receiver_;
    TNode<Object> name_;
    TNode<Object> value_;
    std::optional<TNode<Smi>> flags_;
    TNode<TaggedIndex> slot_;
    TNode<HeapObject> vector_;
    StoreICMode mode_;
  };

  enum class LoadAccessMode { kLoad, kHas };
  enum class ICMode { kNonGlobalIC, kGlobalIC };
  enum ElementSupport { kOnlyProperties, kSupportElements };
  void HandleStoreICHandlerCase(
      const StoreICParameters* p, TNode<MaybeObject> handler, Label* miss,
      ICMode ic_mode, ElementSupport support_elements = kOnlyProperties);
  enum StoreTransitionMapFlags {
    kDontCheckPrototypeValidity = 0,
    kCheckPrototypeValidity = 1 << 0,
    kValidateTransitionHandler = 1 << 1,
    kStoreTransitionMapFlagsMask =
        kCheckPrototypeValidity | kValidateTransitionHandler,
  };
  void HandleStoreICTransitionMapHandlerCase(const StoreICParameters* p,
                                             TNode<Map> transition_map,
                                             Label* miss,
                                             StoreTransitionMapFlags flags);

  // Updates flags on |dict| if |name| is an interesting property.
  void UpdateMayHaveInterestingProperty(TNode<PropertyDictionary> dict,
                                        TNode<Name> name);

  void JumpIfDataProperty(TNode<Uint32T> details, Label* writable,
                          Label* readonly);

  void InvalidateValidityCellIfPrototype(
      TNode<Map> map, std::optional<TNode<Uint32T>> bitfield3 = std::nullopt);

  void OverwriteExistingFastDataProperty(TNode<HeapObject> object,
                                         TNode<Map> object_map,
                                         TNode<DescriptorArray> descriptors,
                                         TNode<IntPtrT> descriptor_name_index,
                                         TNode<Uint32T> details,
                                         TNode<Object> value, Label* slow,
                                         bool do_transitioning_store);

  void StoreJSSharedStructField(TNode<Context> context,
                                TNode<HeapObject> shared_struct,
                                TNode<Map> shared_struct_map,
                                TNode<DescriptorArray> descriptors,
                                TNode<IntPtrT> descriptor_name_index,
                                TNode<Uint32T> details, TNode<Object> value);

  TNode<BoolT> IsPropertyDetailsConst(TNode<Uint32T> details);

  void CheckFieldType(TNode<DescriptorArray> descriptors,
                      TNode<IntPtrT> name_index, TNode<Word32T> representation,
                      TNode<Object> value, Label* bailout);

 private:
  // Stub generation entry points.

  // LoadIC contains the full LoadIC logic, while LoadIC_Noninlined contains
  // logic not inlined into Ignition bytecode handlers.
  void LoadIC(const LoadICParameters* p);

  // Can be used in the receiver != lookup_start_object case.
  void LoadIC_Noninlined(const LoadICParameters* p,
                         TNode<Map> lookup_start_object_map,
                         TNode<HeapObject> feedback,
                         TVariable<MaybeObject>* var_handler, Label* if_handler,
                         Label* miss, ExitPoint* exit_point);

  void LoadSuperIC(const LoadICParameters* p);

  TNode<Object> LoadDescriptorValue(TNode<Map> map,
                                    TNode<IntPtrT> descriptor_entry);
  TNode<MaybeObject> LoadDescriptorValueOrFieldType(
      TNode<Map> map, TNode<IntPtrT> descriptor_entry);

  void LoadIC_NoFeedback(const LoadICParameters* p, TNode<Smi> smi_typeof_mode);
  void LoadSuperIC_NoFeedback(const LoadICParameters* p);
  void LoadGlobalIC_NoFeedback(TNode<Context> context, TNode<Object> name,
                               TNode<Smi> smi_typeof_mode);

  void KeyedLoadIC(const LoadICParameters* p, LoadAccessMode access_mode);
  void KeyedLoadICGeneric(const LoadICParameters* p);
  void KeyedLoadICPolymorphicName(const LoadICParameters* p,
                                  LoadAccessMode access_mode);

  void StoreIC(const StoreICParameters* p);
  void StoreGlobalIC(const StoreICParameters* p);
  void StoreGlobalIC_PropertyCellCase(TNode<PropertyCell> property_cell,
                                      TNode<Object> value,
                                      ExitPoint* exit_point, Label* miss);
  void KeyedStoreIC(const StoreICParameters* p);
  void DefineKeyedOwnIC(const StoreICParameters* p);
  void StoreInArrayLiteralIC(const StoreICParameters* p);

  void LookupGlobalIC(LazyNode<Object> lazy_name, TNode<TaggedIndex> depth,
                      LazyNode<TaggedIndex> lazy_slot, TNode<Context> context,
                      LazyNode<FeedbackVector> lazy_feedback_vector,
                      TypeofMode typeof_mode);
  void LookupContext(LazyNode<Object> lazy_name, TNode<TaggedIndex> depth,
                     LazyNode<TaggedIndex> lazy_slot, TNode<Context> context,
                     TypeofMode typeof_mode, ContextKind context_kind);

  void GotoIfNotSameNumberBitPattern(TNode<Float64T> left,
                                     TNode<Float64T> right, Label* miss);

  // IC dispatcher behavior.

  // Checks monomorphic case. Returns {feedback} entry of the vector.
  TNode<HeapObjectReference> TryMonomorphicCase(
      TNode<TaggedIndex> slot, TNode<FeedbackVector> vector,
      TNode<HeapObjectReference> weak_lookup_start_object_map,
      Label* if_handler, TVariable<MaybeObject>* var_handler, Label* if_miss);
  void HandlePolymorphicCase(
      TNode<HeapObjectReference> weak_lookup_start_object_map,
      TNode<WeakFixedArray> feedback, Label* if_handler,
      TVariable<MaybeObject>* var_handler, Label* if_miss);

  void TryMegaDOMCase(TNode<Object> lookup_start_object,
                      TNode<Map> lookup_start_object_map,
                      TVariable<MaybeObject>* var_handler, TNode<Object> vector,
                      TNode<TaggedIndex> slot, Label* miss,
                      ExitPoint* exit_point);

  void TryEnumeratedKeyedLoad(const LoadICParameters* p,
                              TNode<Map> lookup_start_object_map,
                              ExitPoint* exit_point);

  // LoadIC implementation.
  void HandleLoadICHandlerCase(
      const LazyLoadICParameters* p, TNode<MaybeObject> handler, Label* miss,
      ExitPoint* exit_point, ICMode ic_mode = ICMode::kNonGlobalIC,
      OnNonExistent on_nonexistent = OnNonExistent::kReturnUndefined,
      ElementSupport support_elements = kOnlyProperties,
      LoadAccessMode access_mode = LoadAccessMode::kLoad);

  void HandleLoadICSmiHandlerCase(const LazyLoadICParameters* p,
                                  TNode<Object> holder, TNode<Smi> smi_handler,
                                  TNode<MaybeObject> handler, Label* miss,
                                  ExitPoint* exit_point, ICMode ic_mode,
                                  OnNonExistent on_nonexistent,
                                  ElementSupport support_elements,
                                  LoadAccessMode access_mode);

  void HandleLoadICProtoHandler(const LazyLoadICParameters* p,
                                TNode<DataHandler> handler,
                                TVariable<Object>* var_holder,
                                TVariable<MaybeObject>* var_smi_handler,
                                Label* if_smi_handler, Label* miss,
                                ExitPoint* exit_point, ICMode ic_mode,
                                LoadAccessMode access_mode);

  void HandleLoadCallbackProperty(const LazyLoadICParameters* p,
                                  TNode<JSObject> holder,
                                  TNode<Word32T> handler_word,
                                  ExitPoint* exit_point);

  void HandleLoadAccessor(const LazyLoadICParameters* p,
                          TNode<FunctionTemplateInfo> function_template_info,
                          TNode<Word32T> handler_word,
                          TNode<DataHandler> handler,
                          TNode<Uint32T> handler_kind, ExitPoint* exit_point);

  void HandleLoadField(TNode<JSObject> holder, TNode<Word32T> handler_word,
                       TVariable<Float64T>* var_double_value,
                       Label* rebox_double, Label* miss, ExitPoint* exit_point);

#if V8_ENABLE_WEBASSEMBLY
  void HandleLoadWasmField(TNode<WasmObject> holder,
                           TNode<Int32T> wasm_value_type,
                           TNode<IntPtrT> field_offset,
                           TVariable<Float64T>* var_double_value,
                           Label* rebox_double, ExitPoint* exit_point);

  void HandleLoadWasmField(TNode<WasmObject> holder,
                           TNode<Word32T> handler_word,
                           TVariable<Float64T>* var_double_value,
                           Label* rebox_double, ExitPoint* exit_point);
#endif  // V8_ENABLE_WEBASSEMBLY

  void EmitAccessCheck(TNode<Context> expected_native_context,
                       TNode<Context> context, TNode<Object> receiver,
                       Label* can_access, Label* miss);

  void HandleLoadICSmiHandlerLoadNamedCase(
      const LazyLoadICParameters* p, TNode<Object> holder,
      TNode<Uint32T> handler_kind, TNode<Word32T> handler_word,
      Label* rebox_double, TVariable<Float64T>* var_double_value,
      TNode<MaybeObject> handler, Label* miss, ExitPoint* exit_point,
      ICMode ic_mode, OnNonExistent on_nonexistent,
      ElementSupport support_elements);

  void HandleLoadICSmiHandlerHasNamedCase(const LazyLoadICParameters* p,
                                          TNode<Object> holder,
                                          TNode<Uint32T> handler_kind,
                                          Label* miss, ExitPoint* exit_point,
                                          ICMode ic_mode);

  // LoadGlobalIC implementation.

  void LoadGlobalIC_TryPropertyCellCase(TNode<FeedbackVector> vector,
                                        TNode<TaggedIndex> slot,
                                        const LazyNode<Context>& lazy_context,
                                        ExitPoint* exit_point,
                                        Label* try_handler, Label* miss);

  void LoadGlobalIC_TryHandlerCase(TNode<FeedbackVector> vector,
                                   TNode<TaggedIndex> slot,
                                   const LazyNode<Context>& lazy_context,
                                   const LazyNode<Name>& lazy_name,
                                   TypeofMode typeof_mode,
                                   ExitPoint* exit_point, Label* miss);

  // This is a copy of ScriptContextTable::Lookup. They should be kept in sync.
  void ScriptContextTableLookup(TNode<Name> name,
                                TNode<NativeContext> native_context,
                                Label* found_hole, Label* not_found);

  // StoreIC implementation.

  void HandleStoreICProtoHandler(const StoreICParameters* p,
                                 TNode<StoreHandler> handler, Label* slow,
                                 Label* miss, ICMode ic_mode,
                                 ElementSupport support_elements);
  void HandleStoreICSmiHandlerCase(TNode<Word32T> handler_word,
                                   TNode<JSObject> holder, TNode<Object> value,
                                   Label* miss);
  void HandleStoreICSmiHandlerJSSharedStructFieldCase(
      TNode<Context> context, TNode<Word32T> handler_word,
      TNode<JSObject> holder, TNode<Object> value);
  void HandleStoreFieldAndReturn(TNode<Word32T> handler_word,
                                 TNode<JSObject> holder, TNode<Object> value,
                                 std::optional<TNode<Float64T>> double_value,
                                 Representation representation, Label* miss);

  void CheckPrototypeValidityCell(TNode<Object> maybe_validity_cell,
                                  Label* miss);
  void HandleStoreICNativeDataProperty(const StoreICParameters* p,
                                       TNode<HeapObject> holder,
                                       TNode<Word32T> handler_word);

  void HandleStoreToProxy(const StoreICParameters* p, TNode<JSProxy> proxy,
                          Label* miss, ElementSupport support_elements);

  // KeyedLoadIC_Generic implementation.

  void GenericElementLoad(TNode<HeapObject> lookup_start_object,
                          TNode<Map> lookup_start_object_map,
                          TNode<Int32T> lookup_start_object_instance_type,
                          TNode<IntPtrT> index, Label* slow);

  enum UseStubCache { kUseStubCache, kDontUseStubCache };
  void GenericPropertyLoad(TNode<HeapObject> lookup_start_object,
                           TNode<Map> lookup_start_object_map,
                           TNode<Int32T> lookup_start_object_instance_type,
                           const LoadICParameters* p, Label* slow,
                           UseStubCache use_stub_cache = kUseStubCache);

  // Low-level helpers.

  using OnCodeHandler = std::function<void(TNode<Code> code_handler)>;
  using OnFoundOnLookupStartObject = std::function<void(
      TNode<PropertyDictionary> properties, TNode<IntPtrT> name_index)>;

  template <typename ICHandler, typename ICParameters>
  TNode<Object> HandleProtoHandler(
      const ICParameters* p, TNode<DataHandler> handler,
      const OnCodeHandler& on_code_handler,
      const OnFoundOnLookupStartObject& on_found_on_lookup_start_object,
      Label* miss, ICMode ic_mode);

  void CheckHeapObjectTypeMatchesDescriptor(TNode<Word32T> handler_word,
                                            TNode<JSObject> holder,
                                            TNode<Object> value,
                                            Label* bailout);
  // Double fields store double values in a mutable box, where stores are
  // writes into this box rather than HeapNumber assignment.
  void CheckDescriptorConsidersNumbersMutable(TNode<Word32T> handler_word,
                                              TNode<JSObject> holder,
                                              Label* bailout);

  // Extends properties backing store by JSObject::kFieldsAdded elements,
  // returns updated properties backing store.
  TNode<PropertyArray> ExtendPropertiesBackingStore(TNode<HeapObject> object,
                                                    TNode<IntPtrT> index);

  void EmitFastElementsBoundsCheck(TNode<JSObject> object,
                                   TNode<FixedArrayBase> elements,
                                   TNode<IntPtrT> intptr_index,
                                   TNode<BoolT> is_jsarray_condition,
                                   Label* miss);
  void EmitElementLoad(TNode<HeapObject> object, TNode<Word32T> elements_kind,
                       TNode<IntPtrT> key, TNode<BoolT> is_jsarray_condition,
                       Label* if_hole, Label* rebox_double,
                       TVariable<Float64T>* var_double_value,
                       Label* unimplemented_elements_kind, Label* out_of_bounds,
                       Label* miss, ExitPoint* exit_point,
                       LoadAccessMode access_mode = LoadAccessMode::kLoad);

  // Stub cache access helpers.

  // This enum is used here as a replacement for StubCache::Table to avoid
  // including stub cache header.
  enum StubCacheTable : int;

  TNode<IntPtrT> StubCachePrimaryOffset(TNode<Name> name, TNode<Map> map);
  TNode<IntPtrT> StubCacheSecondaryOffset(TNode<Name> name, TNode<Map> map);

  void TryProbeStubCacheTable(StubCache* stub_cache, StubCacheTable table_id,
                              TNode<IntPtrT> entry_offset, TNode<Object> name,
                              TNode<Map> map, Label* if_handler,
                              TVariable<MaybeObject>* var_handler,
                              Label* if_miss);

  void BranchIfPrototypesHaveNoElements(TNode<Map> receiver_map,
                                        Label* definitely_no_elements,
                                        Label* possibly_elements);
};

// Abstraction over direct and indirect exit points. Direct exits correspond to
// tailcalls and Return, while indirect exits store the result in a variable
// and then jump to an exit label.
class ExitPoint {
 private:
  using CodeAssemblerLabel = compiler::CodeAssemblerLabel;

 public:
  using IndirectReturnHandler = std::function<void(TNode<Object> result)>;

  explicit ExitPoint(CodeStubAssembler* assembler)
      : ExitPoint(assembler, nullptr) {}

  ExitPoint(CodeStubAssembler* assembler,
            const IndirectReturnHandler& indirect_return_handler)
      : asm_(assembler), indirect_return_handler_(indirect_return_handler) {}

  ExitPoint(CodeStubAssembler* assembler, CodeAssemblerLabel* out,
            compiler::CodeAssembler::TVariable<Object>* var_result)
      : ExitPoint(assembler, [=](TNode<Object> result) {
          *var_result = result;
          assembler->Goto(out);
        }) {
    DCHECK_EQ(out != nullptr, var_result != nullptr);
  }

  template <class... TArgs>
  void ReturnCallRuntime(Runtime::FunctionId function, TNode<Context> context,
                         TArgs... args) {
    if (IsDirect()) {
      asm_->TailCallRuntime(function, context, args...);
    } else {
      indirect_return_handler_(asm_->CallRuntime(function, context, args...));
    }
  }

  template <class... TArgs>
  void ReturnCallBuiltin(Builtin builtin, TNode<Context> context,
                         TArgs... args) {
    if (IsDirect()) {
      asm_->TailCallBuiltin(builtin, context, args...);
    } else {
      indirect_return_handler_(asm_->CallBuiltin(builtin, context, args...));
    }
  }

  template <class... TArgs>
  void ReturnCallStub(const CallInterfaceDescriptor& descriptor,
                      TNode<Code> target, TNode<Context> context,
                      TArgs... args) {
    if (IsDirect()) {
      asm_->TailCallStub(descriptor, target, context, args...);
    } else {
      indirect_return_handler_(
          asm_->CallStub(descriptor, target, context, args...));
    }
  }

  void Return(const TNode<Object> result) {
    if (IsDirect()) {
      asm_->Return(result);
    } else {
      indirect_return_handler_(result);
    }
  }

  bool IsDirect() const { return !indirect_return_handler_; }

 private:
  CodeStubAssembler* const asm_;
  IndirectReturnHandler indirect_return_handler_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_ACCESSOR_ASSEMBLER_H_

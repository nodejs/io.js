// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_IC_H_
#define V8_IC_IC_H_

#include <vector>

#include "src/common/message-template.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/ic/stub-cache.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/map.h"
#include "src/objects/maybe-object.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

enum class NamedPropertyType : bool { kNotOwn, kOwn };

//
// IC is the base class for LoadIC, StoreIC, KeyedLoadIC, and KeyedStoreIC.
//
class IC {
 public:
  // Alias the inline cache state type to make the IC code more readable.
  using State = InlineCacheState;

  // Construct the IC structure with the given number of extra
  // JavaScript frames on the stack.
  IC(Isolate* isolate, Handle<FeedbackVector> vector, FeedbackSlot slot,
     FeedbackSlotKind kind);
  virtual ~IC() = default;

  State state() const { return state_; }

  // Compute the current IC state based on the target stub, lookup_start_object
  // and name.
  void UpdateState(Handle<Object> lookup_start_object, Handle<Object> name);

  bool RecomputeHandlerForName(Handle<Object> name);
  void MarkRecomputeHandler(Handle<Object> name) {
    DCHECK(RecomputeHandlerForName(name));
    old_state_ = state_;
    state_ = InlineCacheState::RECOMPUTE_HANDLER;
  }

  bool IsAnyHas() const { return IsKeyedHasIC(); }
  bool IsAnyLoad() const {
    return IsLoadIC() || IsLoadGlobalIC() || IsKeyedLoadIC();
  }
  bool IsAnyStore() const {
    return IsSetNamedIC() || IsDefineNamedOwnIC() || IsStoreGlobalIC() ||
           IsKeyedStoreIC() || IsStoreInArrayLiteralICKind(kind()) ||
           IsDefineKeyedOwnIC();
  }
  bool IsAnyDefineOwn() const {
    return IsDefineNamedOwnIC() || IsDefineKeyedOwnIC();
  }

  static inline bool IsHandler(Tagged<MaybeObject> object);

  // Nofity the IC system that a feedback has changed.
  static void OnFeedbackChanged(Isolate* isolate, Tagged<FeedbackVector> vector,
                                FeedbackSlot slot, const char* reason);

  void OnFeedbackChanged(const char* reason);

 protected:
  void set_slow_stub_reason(const char* reason) { slow_stub_reason_ = reason; }
  void set_accessor(Handle<Object> accessor) { accessor_ = accessor; }
  MaybeHandle<Object> accessor() const { return accessor_; }

  Isolate* isolate() const { return isolate_; }

  bool is_vector_set() { return vector_set_; }
  inline bool vector_needs_update();

  // Configure for most states.
  bool ConfigureVectorState(IC::State new_state, Handle<Object> key);
  // Configure the vector for MONOMORPHIC.
  void ConfigureVectorState(Handle<Name> name, Handle<Map> map,
                            Handle<Object> handler);
  void ConfigureVectorState(Handle<Name> name, Handle<Map> map,
                            const MaybeObjectHandle& handler);
  // Configure the vector for POLYMORPHIC.
  void ConfigureVectorState(Handle<Name> name, MapHandles const& maps,
                            MaybeObjectHandles* handlers);
  void ConfigureVectorState(
      Handle<Name> name, std::vector<MapAndHandler> const& maps_and_handlers);

  char TransitionMarkFromState(IC::State state);
  void TraceIC(const char* type, Handle<Object> name);
  void TraceIC(const char* type, Handle<Object> name, State old_state,
               State new_state);

  MaybeHandle<Object> TypeError(MessageTemplate, Handle<Object> object,
                                Handle<Object> key);
  MaybeHandle<Object> ReferenceError(Handle<Name> name);

  void UpdateMonomorphicIC(const MaybeObjectHandle& handler, Handle<Name> name);
  bool UpdateMegaDOMIC(const MaybeObjectHandle& handler, Handle<Name> name);
  bool UpdatePolymorphicIC(Handle<Name> name, const MaybeObjectHandle& handler);
  void UpdateMegamorphicCache(Handle<Map> map, Handle<Name> name,
                              const MaybeObjectHandle& handler);

  StubCache* stub_cache();

  void CopyICToMegamorphicCache(Handle<Name> name);
  bool IsTransitionOfMonomorphicTarget(Tagged<Map> source_map,
                                       Tagged<Map> target_map);
  void SetCache(Handle<Name> name, Handle<Object> handler);
  void SetCache(Handle<Name> name, const MaybeObjectHandle& handler);
  FeedbackSlotKind kind() const { return kind_; }
  bool IsGlobalIC() const { return IsLoadGlobalIC() || IsStoreGlobalIC(); }
  bool IsLoadIC() const { return IsLoadICKind(kind_); }
  bool IsLoadGlobalIC() const { return IsLoadGlobalICKind(kind_); }
  bool IsKeyedLoadIC() const { return IsKeyedLoadICKind(kind_); }
  bool IsStoreGlobalIC() const { return IsStoreGlobalICKind(kind_); }
  bool IsSetNamedIC() const { return IsSetNamedICKind(kind_); }
  bool IsDefineNamedOwnIC() const { return IsDefineNamedOwnICKind(kind_); }
  bool IsStoreInArrayLiteralIC() const {
    return IsStoreInArrayLiteralICKind(kind_);
  }
  bool IsKeyedStoreIC() const { return IsKeyedStoreICKind(kind_); }
  bool IsKeyedHasIC() const { return IsKeyedHasICKind(kind_); }
  bool IsDefineKeyedOwnIC() const { return IsDefineKeyedOwnICKind(kind_); }
  bool is_keyed() const {
    return IsKeyedLoadIC() || IsKeyedStoreIC() || IsStoreInArrayLiteralIC() ||
           IsKeyedHasIC() || IsDefineKeyedOwnIC();
  }
  bool ShouldRecomputeHandler(Handle<String> name);

  Handle<Map> lookup_start_object_map() { return lookup_start_object_map_; }
  inline void update_lookup_start_object_map(Handle<Object> object);

  void TargetMaps(MapHandles* list) {
    FindTargetMaps();
    for (Handle<Map> map : target_maps_) {
      list->push_back(map);
    }
  }

  Tagged<Map> FirstTargetMap() {
    FindTargetMaps();
    return !target_maps_.empty() ? *target_maps_[0] : Tagged<Map>();
  }

  const FeedbackNexus* nexus() const { return &nexus_; }
  FeedbackNexus* nexus() { return &nexus_; }

 private:
  void FindTargetMaps() {
    if (target_maps_set_) return;
    target_maps_set_ = true;
    nexus()->ExtractMaps(&target_maps_);
  }

  Isolate* isolate_;

  bool vector_set_;
  State old_state_;  // For saving if we marked as prototype failure.
  State state_;
  FeedbackSlotKind kind_;
  Handle<Map> lookup_start_object_map_;
  MaybeHandle<Object> accessor_;
  MapHandles target_maps_;
  bool target_maps_set_;

  const char* slow_stub_reason_;

  FeedbackNexus nexus_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(IC);
};

class LoadIC : public IC {
 public:
  LoadIC(Isolate* isolate, Handle<FeedbackVector> vector, FeedbackSlot slot,
         FeedbackSlotKind kind)
      : IC(isolate, vector, slot, kind) {
    DCHECK(IsAnyLoad() || IsAnyHas());
  }

  static bool ShouldThrowReferenceError(FeedbackSlotKind kind) {
    return kind == FeedbackSlotKind::kLoadGlobalNotInsideTypeof;
  }

  bool ShouldThrowReferenceError() const {
    return ShouldThrowReferenceError(kind());
  }

  // If receiver is empty, use object as the receiver.
  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Load(
      Handle<Object> object, Handle<Name> name, bool update_feedback = true,
      Handle<Object> receiver = Handle<Object>());

 protected:
  // Update the inline cache and the global stub cache based on the
  // lookup result.
  void UpdateCaches(LookupIterator* lookup);

 private:
  MaybeObjectHandle ComputeHandler(LookupIterator* lookup);

  friend class IC;
  friend class NamedLoadHandlerCompiler;
};

class LoadGlobalIC : public LoadIC {
 public:
  LoadGlobalIC(Isolate* isolate, Handle<FeedbackVector> vector,
               FeedbackSlot slot, FeedbackSlotKind kind)
      : LoadIC(isolate, vector, slot, kind) {}

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Load(Handle<Name> name,
                                                 bool update_feedback = true);
};

class KeyedLoadIC : public LoadIC {
 public:
  KeyedLoadIC(Isolate* isolate, Handle<FeedbackVector> vector,
              FeedbackSlot slot, FeedbackSlotKind kind)
      : LoadIC(isolate, vector, slot, kind) {}

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Load(Handle<Object> object,
                                                 Handle<Object> key);

 protected:
  V8_WARN_UNUSED_RESULT MaybeHandle<Object> RuntimeLoad(
      Handle<Object> object, Handle<Object> key, bool* is_found = nullptr);

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> LoadName(Handle<Object> object,
                                                     Handle<Object> key,
                                                     Handle<Name> name);

  // receiver is HeapObject because it could be a String or a JSObject
  void UpdateLoadElement(Handle<HeapObject> receiver,
                         KeyedAccessLoadMode new_load_mode);

 private:
  friend class IC;

  Handle<Object> LoadElementHandler(Handle<Map> receiver_map,
                                    KeyedAccessLoadMode new_load_mode);

  void LoadElementPolymorphicHandlers(MapHandles* receiver_maps,
                                      MaybeObjectHandles* handlers,
                                      KeyedAccessLoadMode new_load_mode);

  KeyedAccessLoadMode GetKeyedAccessLoadModeFor(Handle<Map> receiver_map) const;
};

class StoreIC : public IC {
 public:
  StoreIC(Isolate* isolate, Handle<FeedbackVector> vector, FeedbackSlot slot,
          FeedbackSlotKind kind)
      : IC(isolate, vector, slot, kind) {
    DCHECK(IsAnyStore());
  }

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Store(
      Handle<Object> object, Handle<Name> name, Handle<Object> value,
      StoreOrigin store_origin = StoreOrigin::kNamed);

  bool LookupForWrite(LookupIterator* it, Handle<Object> value,
                      StoreOrigin store_origin);

 protected:
  // Stub accessors.
  // Update the inline cache and the global stub cache based on the
  // lookup result.
  void UpdateCaches(LookupIterator* lookup, Handle<Object> value,
                    StoreOrigin store_origin);

 private:
  MaybeObjectHandle ComputeHandler(LookupIterator* lookup);

  friend class IC;
};

class StoreGlobalIC : public StoreIC {
 public:
  StoreGlobalIC(Isolate* isolate, Handle<FeedbackVector> vector,
                FeedbackSlot slot, FeedbackSlotKind kind)
      : StoreIC(isolate, vector, slot, kind) {}

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Store(Handle<Name> name,
                                                  Handle<Object> value);
};

enum KeyedStoreCheckMap { kDontCheckMap, kCheckMap };

enum KeyedStoreIncrementLength { kDontIncrementLength, kIncrementLength };

enum class TransitionMode {
  kNoTransition,
  kTransitionToDouble,
  kTransitionToObject
};

class KeyedStoreIC : public StoreIC {
 public:
  KeyedAccessStoreMode GetKeyedAccessStoreMode() {
    return nexus()->GetKeyedAccessStoreMode();
  }

  KeyedStoreIC(Isolate* isolate, Handle<FeedbackVector> vector,
               FeedbackSlot slot, FeedbackSlotKind kind)
      : StoreIC(isolate, vector, slot, kind) {}

  V8_WARN_UNUSED_RESULT MaybeHandle<Object> Store(Handle<Object> object,
                                                  Handle<Object> name,
                                                  Handle<Object> value);

 protected:
  void UpdateStoreElement(Handle<Map> receiver_map,
                          KeyedAccessStoreMode store_mode,
                          Handle<Map> new_receiver_map);

 private:
  Handle<Map> ComputeTransitionedMap(Handle<Map> map,
                                     TransitionMode transition_mode);

  Handle<Object> StoreElementHandler(
      Handle<Map> receiver_map, KeyedAccessStoreMode store_mode,
      MaybeHandle<Object> prev_validity_cell = MaybeHandle<Object>());

  void StoreElementPolymorphicHandlers(
      std::vector<MapAndHandler>* receiver_maps_and_handlers,
      KeyedAccessStoreMode store_mode);

  friend class IC;
};

class StoreInArrayLiteralIC : public KeyedStoreIC {
 public:
  StoreInArrayLiteralIC(Isolate* isolate, Handle<FeedbackVector> vector,
                        FeedbackSlot slot)
      : KeyedStoreIC(isolate, vector, slot,
                     FeedbackSlotKind::kStoreInArrayLiteral) {
    DCHECK(IsStoreInArrayLiteralICKind(kind()));
  }

  MaybeHandle<Object> Store(Handle<JSArray> array, Handle<Object> index,
                            Handle<Object> value);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_IC_H_

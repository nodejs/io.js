// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/ic.h"

#include <optional>

#include "src/api/api-arguments-inl.h"
#include "src/ast/ast.h"
#include "src/base/logging.h"
#include "src/builtins/accessors.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/isolate.h"
#include "src/execution/protectors-inl.h"
#include "src/execution/tiering-manager.h"
#include "src/handles/handles-inl.h"
#include "src/handles/maybe-handles.h"
#include "src/ic/call-optimization.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/ic/handler-configuration.h"
#include "src/ic/ic-inl.h"
#include "src/ic/ic-stats.h"
#include "src/ic/stub-cache.h"
#include "src/numbers/conversions.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/field-type.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/megadom-handler.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/prototype.h"
#include "src/runtime/runtime.h"
#include "src/tracing/trace-event.h"
#include "src/tracing/tracing-category-observer.h"
#include "src/utils/ostreams.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/struct-types.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

// Aliases to avoid having to repeat the class.
// With C++20 we can use "using" to introduce scoped enums.
constexpr InlineCacheState NO_FEEDBACK = InlineCacheState::NO_FEEDBACK;
constexpr InlineCacheState UNINITIALIZED = InlineCacheState::UNINITIALIZED;
constexpr InlineCacheState MONOMORPHIC = InlineCacheState::MONOMORPHIC;
constexpr InlineCacheState RECOMPUTE_HANDLER =
    InlineCacheState::RECOMPUTE_HANDLER;
constexpr InlineCacheState POLYMORPHIC = InlineCacheState::POLYMORPHIC;
constexpr InlineCacheState MEGAMORPHIC = InlineCacheState::MEGAMORPHIC;
constexpr InlineCacheState MEGADOM = InlineCacheState::MEGADOM;
constexpr InlineCacheState GENERIC = InlineCacheState::GENERIC;

char IC::TransitionMarkFromState(IC::State state) {
  switch (state) {
    case NO_FEEDBACK:
      return 'X';
    case UNINITIALIZED:
      return '0';
    case MONOMORPHIC:
      return '1';
    case RECOMPUTE_HANDLER:
      return '^';
    case POLYMORPHIC:
      return 'P';
    case MEGAMORPHIC:
      return 'N';
    case MEGADOM:
      return 'D';
    case GENERIC:
      return 'G';
  }
  UNREACHABLE();
}

namespace {

const char* GetModifier(KeyedAccessLoadMode mode) {
  switch (mode) {
    case KeyedAccessLoadMode::kHandleOOB:
      return ".OOB";
    case KeyedAccessLoadMode::kHandleHoles:
      return ".HOLES";
    case KeyedAccessLoadMode::kHandleOOBAndHoles:
      return ".OOB+HOLES";
    case KeyedAccessLoadMode::kInBounds:
      return "";
  }
}

const char* GetModifier(KeyedAccessStoreMode mode) {
  switch (mode) {
    case KeyedAccessStoreMode::kHandleCOW:
      return ".COW";
    case KeyedAccessStoreMode::kGrowAndHandleCOW:
      return ".STORE+COW";
    case KeyedAccessStoreMode::kIgnoreTypedArrayOOB:
      return ".IGNORE_OOB";
    case KeyedAccessStoreMode::kInBounds:
      return "";
  }
  UNREACHABLE();
}

}  // namespace

void IC::TraceIC(const char* type, DirectHandle<Object> name) {
  if (V8_LIKELY(!TracingFlags::is_ic_stats_enabled())) return;
  State new_state =
      (state() == NO_FEEDBACK) ? NO_FEEDBACK : nexus()->ic_state();
  TraceIC(type, name, state(), new_state);
}

void IC::TraceIC(const char* type, DirectHandle<Object> name, State old_state,
                 State new_state) {
  if (V8_LIKELY(!TracingFlags::is_ic_stats_enabled())) return;

  Handle<Map> map = lookup_start_object_map();  // Might be empty.

  const char* modifier = "";
  if (state() == NO_FEEDBACK) {
    modifier = "";
  } else if (IsKeyedLoadIC()) {
    KeyedAccessLoadMode mode = nexus()->GetKeyedAccessLoadMode();
    modifier = GetModifier(mode);
  } else if (IsKeyedStoreIC() || IsStoreInArrayLiteralIC() ||
             IsDefineKeyedOwnIC()) {
    KeyedAccessStoreMode mode = nexus()->GetKeyedAccessStoreMode();
    modifier = GetModifier(mode);
  }

  bool keyed_prefix = is_keyed() && !IsStoreInArrayLiteralIC();

  if (!(TracingFlags::ic_stats.load(std::memory_order_relaxed) &
        v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
    LOG(isolate(), ICEvent(type, keyed_prefix, map, name,
                           TransitionMarkFromState(old_state),
                           TransitionMarkFromState(new_state), modifier,
                           slow_stub_reason_));
    return;
  }

  JavaScriptStackFrameIterator it(isolate());
  JavaScriptFrame* frame = it.frame();

  DisallowGarbageCollection no_gc;
  Tagged<JSFunction> function = frame->function();

  ICStats::instance()->Begin();
  ICInfo& ic_info = ICStats::instance()->Current();
  ic_info.type = keyed_prefix ? "Keyed" : "";
  ic_info.type += type;

  int code_offset = 0;
  Tagged<AbstractCode> code = function->abstract_code(isolate_);
  if (function->ActiveTierIsIgnition(isolate())) {
    code_offset = InterpretedFrame::GetBytecodeOffset(frame->fp());
  } else if (function->ActiveTierIsBaseline(isolate())) {
    // TODO(pthier): AbstractCode should fully support Baseline code.
    BaselineFrame* baseline_frame = BaselineFrame::cast(frame);
    code_offset = baseline_frame->GetBytecodeOffset();
    code = Cast<AbstractCode>(baseline_frame->GetBytecodeArray());
  } else {
    code_offset =
        static_cast<int>(frame->pc() - function->instruction_start(isolate()));
  }
  JavaScriptFrame::CollectFunctionAndOffsetForICStats(isolate(), function, code,
                                                      code_offset);

  // Reserve enough space for IC transition state, the longest length is 17.
  ic_info.state.reserve(17);
  ic_info.state = "(";
  ic_info.state += TransitionMarkFromState(old_state);
  ic_info.state += "->";
  ic_info.state += TransitionMarkFromState(new_state);
  ic_info.state += modifier;
  ic_info.state += ")";
  if (!map.is_null()) {
    ic_info.map = reinterpret_cast<void*>(map->ptr());
    ic_info.is_dictionary_map = map->is_dictionary_map();
    ic_info.number_of_own_descriptors = map->NumberOfOwnDescriptors();
    ic_info.instance_type = std::to_string(map->instance_type());
  } else {
    ic_info.map = nullptr;
  }
  // TODO(lpy) Add name as key field in ICStats.
  ICStats::instance()->End();
}

IC::IC(Isolate* isolate, Handle<FeedbackVector> vector, FeedbackSlot slot,
       FeedbackSlotKind kind)
    : isolate_(isolate),
      vector_set_(false),
      kind_(kind),
      target_maps_set_(false),
      slow_stub_reason_(nullptr),
      nexus_(isolate, vector, slot) {
  DCHECK_IMPLIES(!vector.is_null(), kind_ == nexus_.kind());
  state_ = (vector.is_null()) ? NO_FEEDBACK : nexus_.ic_state();
  old_state_ = state_;
}

static void LookupForRead(LookupIterator* it, bool is_has_property) {
  for (;; it->Next()) {
    switch (it->state()) {
      case LookupIterator::TRANSITION:
        UNREACHABLE();
      case LookupIterator::JSPROXY:
      case LookupIterator::WASM_OBJECT:
        return;
      case LookupIterator::INTERCEPTOR: {
        // If there is a getter, return; otherwise loop to perform the lookup.
        DirectHandle<JSObject> holder = it->GetHolder<JSObject>();
        if (!IsUndefined(holder->GetNamedInterceptor()->getter(),
                         it->isolate())) {
          return;
        }
        if (is_has_property &&
            !IsUndefined(holder->GetNamedInterceptor()->query(),
                         it->isolate())) {
          return;
        }
        continue;
      }
      case LookupIterator::ACCESS_CHECK:
        // ICs know how to perform access checks on global proxies.
        if (it->GetHolder<JSObject>().is_identical_to(
                it->isolate()->global_proxy()) &&
            !it->isolate()->global_object()->IsDetached()) {
          continue;
        }
        return;
      case LookupIterator::ACCESSOR:
      case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
      case LookupIterator::DATA:
      case LookupIterator::NOT_FOUND:
        return;
    }
    UNREACHABLE();
  }
}

bool IC::ShouldRecomputeHandler(DirectHandle<String> name) {
  if (!RecomputeHandlerForName(name)) return false;

  // This is a contextual access, always just update the handler and stay
  // monomorphic.
  if (IsGlobalIC()) return true;

  MaybeObjectHandle maybe_handler =
      nexus()->FindHandlerForMap(lookup_start_object_map());

  // The current map wasn't handled yet. There's no reason to stay monomorphic,
  // *unless* we're moving from a deprecated map to its replacement, or
  // to a more general elements kind.
  // TODO(verwaest): Check if the current map is actually what the old map
  // would transition to.
  if (maybe_handler.is_null()) {
    if (!IsJSObjectMap(*lookup_start_object_map())) return false;
    Tagged<Map> first_map = FirstTargetMap();
    if (first_map.is_null()) return false;
    DirectHandle<Map> old_map(first_map, isolate());
    if (old_map->is_deprecated()) return true;
    return IsMoreGeneralElementsKindTransition(
        old_map->elements_kind(), lookup_start_object_map()->elements_kind());
  }

  return true;
}

bool IC::RecomputeHandlerForName(DirectHandle<Object> name) {
  if (is_keyed()) {
    // Determine whether the failure is due to a name failure.
    if (!IsName(*name)) return false;
    Tagged<Name> stub_name = nexus()->GetName();
    if (*name != stub_name) return false;
  }

  return true;
}

void IC::UpdateState(DirectHandle<Object> lookup_start_object,
                     Handle<Object> name) {
  if (state() == NO_FEEDBACK) return;
  update_lookup_start_object_map(lookup_start_object);
  if (!IsString(*name)) return;
  if (state() != MONOMORPHIC && state() != POLYMORPHIC) return;
  if (IsNullOrUndefined(*lookup_start_object, isolate())) return;

  // Remove the target from the code cache if it became invalid
  // because of changes in the prototype chain to avoid hitting it
  // again.
  if (ShouldRecomputeHandler(Cast<String>(name))) {
    MarkRecomputeHandler(name);
  }
}

MaybeHandle<Object> IC::TypeError(MessageTemplate index, Handle<Object> object,
                                  Handle<Object> key) {
  HandleScope scope(isolate());
  THROW_NEW_ERROR(isolate(), NewTypeError(index, key, object));
}

MaybeHandle<Object> IC::ReferenceError(Handle<Name> name) {
  HandleScope scope(isolate());
  THROW_NEW_ERROR(isolate(),
                  NewReferenceError(MessageTemplate::kNotDefined, name));
}

void IC::OnFeedbackChanged(const char* reason) {
  vector_set_ = true;
  Tagged<FeedbackVector> vector = nexus()->vector();
  FeedbackSlot slot = nexus()->slot();
  OnFeedbackChanged(isolate(), vector, slot, reason);
}

// static
void IC::OnFeedbackChanged(Isolate* isolate, Tagged<FeedbackVector> vector,
                           FeedbackSlot slot, const char* reason) {
#ifdef V8_TRACE_FEEDBACK_UPDATES
  if (v8_flags.trace_feedback_updates) {
    FeedbackVector::TraceFeedbackChange(isolate, vector, slot, reason);
  }
#endif

  isolate->tiering_manager()->NotifyICChanged(vector);
}

namespace {

bool MigrateDeprecated(Isolate* isolate, Handle<Object> object) {
  if (!IsJSObject(*object)) return false;
  DirectHandle<JSObject> receiver = Cast<JSObject>(object);
  if (!receiver->map()->is_deprecated()) return false;
  JSObject::MigrateInstance(isolate, receiver);
  return true;
}

}  // namespace

bool IC::ConfigureVectorState(IC::State new_state, DirectHandle<Object> key) {
  DCHECK_EQ(MEGAMORPHIC, new_state);
  DCHECK_IMPLIES(!is_keyed(), IsName(*key));
  bool changed = nexus()->ConfigureMegamorphic(
      IsName(*key) ? IcCheckType::kProperty : IcCheckType::kElement);
  if (changed) {
    OnFeedbackChanged("Megamorphic");
  }
  return changed;
}

void IC::ConfigureVectorState(Handle<Name> name, DirectHandle<Map> map,
                              Handle<Object> handler) {
  ConfigureVectorState(name, map, MaybeObjectHandle(handler));
}

void IC::ConfigureVectorState(Handle<Name> name, DirectHandle<Map> map,
                              const MaybeObjectHandle& handler) {
  if (IsGlobalIC()) {
    nexus()->ConfigureHandlerMode(handler);
  } else {
    // Non-keyed ICs don't track the name explicitly.
    if (!is_keyed()) name = Handle<Name>::null();
    nexus()->ConfigureMonomorphic(name, map, handler);
  }

  OnFeedbackChanged(IsLoadGlobalIC() ? "LoadGlobal" : "Monomorphic");
}

void IC::ConfigureVectorState(Handle<Name> name, MapHandlesSpan maps,
                              MaybeObjectHandles* handlers) {
  DCHECK(!IsGlobalIC());
  std::vector<MapAndHandler> maps_and_handlers;
  DCHECK_EQ(maps.size(), handlers->size());
  for (size_t i = 0; i < maps.size(); i++) {
    maps_and_handlers.push_back(MapAndHandler(maps[i], handlers->at(i)));
  }
  ConfigureVectorState(name, maps_and_handlers);
}

void IC::ConfigureVectorState(
    Handle<Name> name, std::vector<MapAndHandler> const& maps_and_handlers) {
  DCHECK(!IsGlobalIC());
  // Non-keyed ICs don't track the name explicitly.
  if (!is_keyed()) name = Handle<Name>::null();
  nexus()->ConfigurePolymorphic(name, maps_and_handlers);

  OnFeedbackChanged("Polymorphic");
}

MaybeHandle<Object> LoadIC::Load(Handle<Object> object, Handle<Name> name,
                                 bool update_feedback,
                                 Handle<Object> receiver) {
  bool use_ic = (state() != NO_FEEDBACK) && v8_flags.use_ic && update_feedback;

  if (receiver.is_null()) {
    receiver = object;
  }

  // If the object is undefined or null it's illegal to try to get any
  // of its properties; throw a TypeError in that case.
  if (IsAnyHas() ? !IsJSReceiver(*object)
                 : IsNullOrUndefined(*object, isolate())) {
    if (use_ic) {
      // Ensure the IC state progresses.
      TRACE_HANDLER_STATS(isolate(), LoadIC_NonReceiver);
      update_lookup_start_object_map(object);
      SetCache(name, LoadHandler::LoadSlow(isolate()));
      TraceIC("LoadIC", name);
    }

    if (*name == ReadOnlyRoots(isolate()).iterator_symbol()) {
      isolate()->Throw(*ErrorUtils::NewIteratorError(isolate(), object));
      return MaybeHandle<Object>();
    }

    if (IsAnyHas()) {
      return TypeError(MessageTemplate::kInvalidInOperatorUse, object, name);
    } else {
      DCHECK(IsNullOrUndefined(*object, isolate()));
      ErrorUtils::ThrowLoadFromNullOrUndefined(isolate(), object, name);
      return MaybeHandle<Object>();
    }
  }

  // If we encounter an object with a deprecated map, we want to update the
  // feedback vector with the migrated map.
  // Mark ourselves as RECOMPUTE_HANDLER so that we don't turn megamorphic due
  // to seeing the same map and handler.
  if (MigrateDeprecated(isolate(), object)) {
    UpdateState(object, name);
  }

  JSObject::MakePrototypesFast(object, kStartAtReceiver, isolate());
  update_lookup_start_object_map(object);

  PropertyKey key(isolate(), name);
  LookupIterator it = LookupIterator(isolate(), receiver, key, object);

  // Named lookup in the object.
  LookupForRead(&it, IsAnyHas());

  if (it.IsFound() || !ShouldThrowReferenceError()) {
    // Update inline cache and stub cache.
    if (use_ic) {
      UpdateCaches(&it);
    } else if (state() == NO_FEEDBACK) {
      // Tracing IC stats
      IsLoadGlobalIC() ? TraceIC("LoadGlobalIC", name)
                       : TraceIC("LoadIC", name);
    }

    if (IsAnyHas()) {
      // Named lookup in the object.
      Maybe<bool> maybe = JSReceiver::HasProperty(&it);
      if (maybe.IsNothing()) return MaybeHandle<Object>();
      return isolate()->factory()->ToBoolean(maybe.FromJust());
    }

    // Get the property.
    Handle<Object> result;

    ASSIGN_RETURN_ON_EXCEPTION(isolate(), result,
                               Object::GetProperty(&it, IsLoadGlobalIC()));
    if (it.IsFound()) {
      return result;
    } else if (!ShouldThrowReferenceError()) {
      return result;
    }
  }
  return ReferenceError(name);
}

MaybeHandle<Object> LoadGlobalIC::Load(Handle<Name> name,
                                       bool update_feedback) {
  Handle<JSGlobalObject> global = isolate()->global_object();

  if (IsString(*name)) {
    // Look up in script context table.
    Handle<String> str_name = Cast<String>(name);
    DirectHandle<ScriptContextTable> script_contexts(
        global->native_context()->script_context_table(), isolate());

    VariableLookupResult lookup_result;
    if (script_contexts->Lookup(str_name, &lookup_result)) {
      DirectHandle<Context> script_context(
          script_contexts->get(lookup_result.context_index), isolate());
      Handle<Object> result(script_context->get(lookup_result.slot_index),
                            isolate());

      if (IsTheHole(*result, isolate())) {
        // Do not install stubs and stay pre-monomorphic for
        // uninitialized accesses.
        THROW_NEW_ERROR(
            isolate(),
            NewReferenceError(MessageTemplate::kAccessedUninitializedVariable,
                              name));
      }

      bool use_ic =
          (state() != NO_FEEDBACK) && v8_flags.use_ic && update_feedback;
      if (use_ic) {
        // 'const' Variables are mutable if REPL mode is enabled. This disables
        // compiler inlining for all 'const' variables declared in REPL mode.
        if (nexus()->ConfigureLexicalVarMode(
                lookup_result.context_index, lookup_result.slot_index,
                (IsImmutableLexicalVariableMode(lookup_result.mode) &&
                 !lookup_result.is_repl_mode))) {
          TRACE_HANDLER_STATS(isolate(), LoadGlobalIC_LoadScriptContextField);
        } else {
          // Given combination of indices can't be encoded, so use slow stub.
          TRACE_HANDLER_STATS(isolate(), LoadGlobalIC_SlowStub);
          SetCache(name, LoadHandler::LoadSlow(isolate()));
        }
        TraceIC("LoadGlobalIC", name);
      } else if (state() == NO_FEEDBACK) {
        TraceIC("LoadGlobalIC", name);
      }
      return result;
    }
  }
  return LoadIC::Load(global, name, update_feedback);
}

namespace {

bool AddOneReceiverMapIfMissing(MapHandles* receiver_maps,
                                Handle<Map> new_receiver_map) {
  DCHECK(!new_receiver_map.is_null());
  for (Handle<Map> map : *receiver_maps) {
    if (!map.is_null() && map.is_identical_to(new_receiver_map)) {
      return false;
    }
  }
  receiver_maps->push_back(new_receiver_map);
  return true;
}

bool AddOneReceiverMapIfMissing(
    std::vector<MapAndHandler>* receiver_maps_and_handlers,
    Handle<Map> new_receiver_map) {
  DCHECK(!new_receiver_map.is_null());
  if (new_receiver_map->is_deprecated()) return false;
  for (MapAndHandler map_and_handler : *receiver_maps_and_handlers) {
    Handle<Map> map = map_and_handler.first;
    if (!map.is_null() && map.is_identical_to(new_receiver_map)) {
      return false;
    }
  }
  receiver_maps_and_handlers->push_back(
      MapAndHandler(new_receiver_map, MaybeObjectHandle()));
  return true;
}

Handle<NativeContext> GetAccessorContext(
    const CallOptimization& call_optimization, Tagged<Map> holder_map,
    Isolate* isolate) {
  std::optional<Tagged<NativeContext>> maybe_context =
      call_optimization.GetAccessorContext(holder_map);

  // Holders which are remote objects are not expected in the IC system.
  CHECK(maybe_context.has_value());
  return handle(maybe_context.value(), isolate);
}

}  // namespace

bool IC::UpdateMegaDOMIC(const MaybeObjectHandle& handler,
                         DirectHandle<Name> name) {
  if (!v8_flags.mega_dom_ic) return false;

  // TODO(gsathya): Enable fuzzing once this feature is more stable.
  if (v8_flags.fuzzing) return false;

  // TODO(gsathya): Support KeyedLoadIC, StoreIC and KeyedStoreIC.
  if (!IsLoadIC()) return false;

  // Check if DOM protector cell is valid.
  if (!Protectors::IsMegaDOMIntact(isolate())) return false;

  // Check if current lookup object is an API object
  Handle<Map> map = lookup_start_object_map();
  if (!InstanceTypeChecker::IsJSApiObject(map->instance_type())) return false;

  Handle<Object> accessor_obj;
  // TODO(gsathya): Check if there are overloads possible for this accessor and
  // transition only if it isn't possible.
  if (!accessor().ToHandle(&accessor_obj)) return false;

  // TODO(gsathya): This is also created in IC::ComputeHandler, find a way to
  // reuse it here.
  CallOptimization call_optimization(isolate(), accessor_obj);

  // Check if accessor is an API function
  if (!call_optimization.is_simple_api_call()) return false;

  // Check if accessor requires access checks
  if (call_optimization.accept_any_receiver()) return false;

  // Check if accessor requires signature checks
  if (!call_optimization.requires_signature_check()) return false;

  // Check if the receiver is the holder
  CallOptimization::HolderLookup holder_lookup;
  call_optimization.LookupHolderOfExpectedType(isolate(), map, &holder_lookup);
  if (holder_lookup != CallOptimization::kHolderIsReceiver) return false;

  Handle<NativeContext> accessor_context =
      GetAccessorContext(call_optimization, *map, isolate());

  Handle<FunctionTemplateInfo> fti;
  if (IsJSFunction(*accessor_obj)) {
    fti = handle(Cast<JSFunction>(*accessor_obj)->shared()->api_func_data(),
                 isolate());
  } else {
    fti = Cast<FunctionTemplateInfo>(accessor_obj);
  }

  Handle<MegaDomHandler> new_handler = isolate()->factory()->NewMegaDomHandler(
      MaybeObjectHandle::Weak(fti), MaybeObjectHandle::Weak(accessor_context));
  nexus()->ConfigureMegaDOM(MaybeObjectHandle(new_handler));
  return true;
}

bool IC::UpdatePolymorphicIC(Handle<Name> name,
                             const MaybeObjectHandle& handler) {
  DCHECK(IsHandler(*handler));
  if (is_keyed() && state() != RECOMPUTE_HANDLER) {
    if (nexus()->GetName() != *name) return false;
  }
  Handle<Map> map = lookup_start_object_map();

  std::vector<MapAndHandler> maps_and_handlers;
  maps_and_handlers.reserve(v8_flags.max_valid_polymorphic_map_count);
  int deprecated_maps = 0;
  int handler_to_overwrite = -1;

  {
    DisallowGarbageCollection no_gc;
    int i = 0;
    for (FeedbackIterator it(nexus()); !it.done(); it.Advance()) {
      if (it.handler().IsCleared()) continue;
      MaybeObjectHandle existing_handler = handle(it.handler(), isolate());
      Handle<Map> existing_map = handle(it.map(), isolate());

      maps_and_handlers.push_back(
          MapAndHandler(existing_map, existing_handler));

      if (existing_map->is_deprecated()) {
        // Filter out deprecated maps to ensure their instances get migrated.
        deprecated_maps++;
      } else if (map.is_identical_to(existing_map)) {
        // If both map and handler stayed the same (and the name is also the
        // same as checked above, for keyed accesses), we're not progressing
        // in the lattice and need to go MEGAMORPHIC instead. There's one
        // exception to this rule, which is when we're in RECOMPUTE_HANDLER
        // state, there we allow to migrate to a new handler.
        if (handler.is_identical_to(existing_handler) &&
            state() != RECOMPUTE_HANDLER) {
          return false;
        }

        // If the receiver type is already in the polymorphic IC, this indicates
        // there was a prototoype chain failure. In that case, just overwrite
        // the handler.
        handler_to_overwrite = i;
      } else if (handler_to_overwrite == -1 &&
                 IsTransitionOfMonomorphicTarget(*existing_map, *map)) {
        handler_to_overwrite = i;
      }

      i++;
    }
    DCHECK_LE(i, maps_and_handlers.size());
  }

  int number_of_maps = static_cast<int>(maps_and_handlers.size());
  int number_of_valid_maps =
      number_of_maps - deprecated_maps - (handler_to_overwrite != -1);

  if (number_of_valid_maps >= v8_flags.max_valid_polymorphic_map_count) {
    return false;
  }
  if (deprecated_maps >= v8_flags.max_valid_polymorphic_map_count) {
    return false;
  }
  if (number_of_maps == 0 && state() != MONOMORPHIC && state() != POLYMORPHIC) {
    return false;
  }

  number_of_valid_maps++;
  if (number_of_valid_maps == 1) {
    ConfigureVectorState(name, lookup_start_object_map(), handler);
  } else {
    if (is_keyed() && nexus()->GetName() != *name) return false;
    if (handler_to_overwrite >= 0) {
      maps_and_handlers[handler_to_overwrite].second = handler;
      if (!map.is_identical_to(
              maps_and_handlers.at(handler_to_overwrite).first)) {
        maps_and_handlers[handler_to_overwrite].first = map;
      }
    } else {
      maps_and_handlers.push_back(MapAndHandler(map, handler));
    }

    ConfigureVectorState(name, maps_and_handlers);
  }

  return true;
}

void IC::UpdateMonomorphicIC(const MaybeObjectHandle& handler,
                             Handle<Name> name) {
  DCHECK(IsHandler(*handler));
  ConfigureVectorState(name, lookup_start_object_map(), handler);
}

void IC::CopyICToMegamorphicCache(DirectHandle<Name> name) {
  std::vector<MapAndHandler> maps_and_handlers;
  nexus()->ExtractMapsAndHandlers(&maps_and_handlers);
  for (const MapAndHandler& map_and_handler : maps_and_handlers) {
    UpdateMegamorphicCache(map_and_handler.first, name, map_and_handler.second);
  }
}

bool IC::IsTransitionOfMonomorphicTarget(Tagged<Map> source_map,
                                         Tagged<Map> target_map) {
  if (source_map.is_null()) return true;
  if (target_map.is_null()) return false;
  if (source_map->is_abandoned_prototype_map()) return false;
  ElementsKind target_elements_kind = target_map->elements_kind();
  bool more_general_transition = IsMoreGeneralElementsKindTransition(
      source_map->elements_kind(), target_elements_kind);
  Tagged<Map> transitioned_map;
  if (more_general_transition) {
    Handle<Map> single_map[1] = {handle(target_map, isolate_)};
    transitioned_map = source_map->FindElementsKindTransitionedMap(
        isolate(), single_map, ConcurrencyMode::kSynchronous);
  }
  return transitioned_map == target_map;
}

void IC::SetCache(Handle<Name> name, Handle<Object> handler) {
  SetCache(name, MaybeObjectHandle(handler));
}

void IC::SetCache(Handle<Name> name, const MaybeObjectHandle& handler) {
  DCHECK(IsHandler(*handler));
  // Currently only load and store ICs support non-code handlers.
  DCHECK(IsAnyLoad() || IsAnyStore() || IsAnyHas());
  switch (state()) {
    case NO_FEEDBACK:
      UNREACHABLE();
    case UNINITIALIZED:
      UpdateMonomorphicIC(handler, name);
      break;
    case RECOMPUTE_HANDLER:
    case MONOMORPHIC:
      if (IsGlobalIC()) {
        UpdateMonomorphicIC(handler, name);
        break;
      }
      [[fallthrough]];
    case POLYMORPHIC:
      if (UpdatePolymorphicIC(name, handler)) break;
      if (UpdateMegaDOMIC(handler, name)) break;
      if (!is_keyed() || state() == RECOMPUTE_HANDLER) {
        CopyICToMegamorphicCache(name);
      }
      [[fallthrough]];
    case MEGADOM:
      ConfigureVectorState(MEGAMORPHIC, name);
      [[fallthrough]];
    case MEGAMORPHIC:
      UpdateMegamorphicCache(lookup_start_object_map(), name, handler);
      // Indicate that we've handled this case.
      vector_set_ = true;
      break;
    case GENERIC:
      UNREACHABLE();
  }
}

void LoadIC::UpdateCaches(LookupIterator* lookup) {
  MaybeObjectHandle handler;
  if (lookup->state() == LookupIterator::ACCESS_CHECK) {
    handler = MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
  } else if (!lookup->IsFound()) {
    if (lookup->IsPrivateName()) {
      handler = MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
    } else {
      TRACE_HANDLER_STATS(isolate(), LoadIC_LoadNonexistentDH);
      Handle<Smi> smi_handler = LoadHandler::LoadNonExistent(isolate());
      handler = MaybeObjectHandle(LoadHandler::LoadFullChain(
          isolate(), lookup_start_object_map(),
          MaybeObjectHandle(isolate()->factory()->null_value()), smi_handler));
    }
  } else if (IsLoadGlobalIC() && lookup->state() == LookupIterator::JSPROXY) {
    // If there is proxy just install the slow stub since we need to call the
    // HasProperty trap for global loads. The ProxyGetProperty builtin doesn't
    // handle this case.
    handler = MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
  } else {
    if (IsLoadGlobalIC()) {
      if (lookup->TryLookupCachedProperty()) {
        DCHECK_EQ(LookupIterator::DATA, lookup->state());
      }
      if (lookup->state() == LookupIterator::DATA &&
          lookup->GetReceiver().is_identical_to(lookup->GetHolder<Object>())) {
        DCHECK(IsJSGlobalObject(*lookup->GetReceiver()));
        // Now update the cell in the feedback vector.
        nexus()->ConfigurePropertyCellMode(lookup->GetPropertyCell());
        TraceIC("LoadGlobalIC", lookup->name());
        return;
      }
    }
    handler = ComputeHandler(lookup);
    auto holder = lookup->GetHolder<Object>();
    CHECK(*holder == *(lookup->lookup_start_object()) ||
          LoadHandler::CanHandleHolderNotLookupStart(*handler.object()) ||
          IsJSPrimitiveWrapper(*holder));
  }
  // Can't use {lookup->name()} because the LookupIterator might be in
  // "elements" mode for keys that are strings representing integers above
  // JSArray::kMaxIndex.
  SetCache(lookup->GetName(), handler);
  TraceIC("LoadIC", lookup->GetName());
}

StubCache* IC::stub_cache() {
  // HasICs and each of the store own ICs require its own stub cache.
  // Until we create them, don't allow accessing the load/store stub caches.
  DCHECK(!IsAnyHas());
  if (IsAnyLoad()) {
    return isolate()->load_stub_cache();
  } else if (IsAnyDefineOwn()) {
    return isolate()->define_own_stub_cache();
  } else {
    DCHECK(IsAnyStore());
    return isolate()->store_stub_cache();
  }
}

void IC::UpdateMegamorphicCache(DirectHandle<Map> map, DirectHandle<Name> name,
                                const MaybeObjectHandle& handler) {
  if (!IsAnyHas()) {
    stub_cache()->Set(*name, *map, *handler);
  }
}

MaybeObjectHandle LoadIC::ComputeHandler(LookupIterator* lookup) {
  DirectHandle<Object> receiver = lookup->GetReceiver();
  ReadOnlyRoots roots(isolate());

  Handle<Object> lookup_start_object = lookup->lookup_start_object();
  // `in` cannot be called on strings, and will always return true for string
  // wrapper length and function prototypes. The latter two cases are given
  // LoadHandler::LoadNativeDataProperty below.
  if (!IsAnyHas() && !lookup->IsElement()) {
    if (IsString(*lookup_start_object) &&
        *lookup->name() == roots.length_string()) {
      TRACE_HANDLER_STATS(isolate(), LoadIC_StringLength);
      return MaybeObjectHandle(BUILTIN_CODE(isolate(), LoadIC_StringLength));
    }

    if (IsStringWrapper(*lookup_start_object) &&
        *lookup->name() == roots.length_string()) {
      TRACE_HANDLER_STATS(isolate(), LoadIC_StringWrapperLength);
      return MaybeObjectHandle(
          BUILTIN_CODE(isolate(), LoadIC_StringWrapperLength));
    }

    // Use specialized code for getting prototype of functions.
    if (IsJSFunction(*lookup_start_object) &&
        *lookup->name() == roots.prototype_string() &&
        !Cast<JSFunction>(*lookup_start_object)
             ->PrototypeRequiresRuntimeLookup()) {
      TRACE_HANDLER_STATS(isolate(), LoadIC_FunctionPrototypeStub);
      return MaybeObjectHandle(
          BUILTIN_CODE(isolate(), LoadIC_FunctionPrototype));
    }
  }

  Handle<Map> map = lookup_start_object_map();
  bool holder_is_lookup_start_object =
      lookup_start_object.is_identical_to(lookup->GetHolder<JSReceiver>());

  switch (lookup->state()) {
    case LookupIterator::INTERCEPTOR: {
      Handle<JSObject> holder = lookup->GetHolder<JSObject>();
      Handle<Smi> smi_handler = LoadHandler::LoadInterceptor(isolate());

      if (holder->GetNamedInterceptor()->non_masking()) {
        MaybeObjectHandle holder_ref(isolate()->factory()->null_value());
        if (!holder_is_lookup_start_object || IsLoadGlobalIC()) {
          holder_ref = MaybeObjectHandle::Weak(holder);
        }
        TRACE_HANDLER_STATS(isolate(), LoadIC_LoadNonMaskingInterceptorDH);
        return MaybeObjectHandle(LoadHandler::LoadFullChain(
            isolate(), map, holder_ref, smi_handler));
      }

      if (holder_is_lookup_start_object) {
        DCHECK(map->has_named_interceptor());
        TRACE_HANDLER_STATS(isolate(), LoadIC_LoadInterceptorDH);
        return MaybeObjectHandle(smi_handler);
      }

      TRACE_HANDLER_STATS(isolate(), LoadIC_LoadInterceptorFromPrototypeDH);
      return MaybeObjectHandle(
          LoadHandler::LoadFromPrototype(isolate(), map, holder, *smi_handler));
    }

    case LookupIterator::ACCESSOR: {
      Handle<JSObject> holder = lookup->GetHolder<JSObject>();
      // Use simple field loads for some well-known callback properties.
      // The method will only return true for absolute truths based on the
      // lookup start object maps.
      FieldIndex field_index;
      if (Accessors::IsJSObjectFieldAccessor(isolate(), map, lookup->name(),
                                             &field_index)) {
        TRACE_HANDLER_STATS(isolate(), LoadIC_LoadFieldDH);
        return MaybeObjectHandle(
            LoadHandler::LoadField(isolate(), field_index));
      }
      if (IsJSModuleNamespace(*holder)) {
        DirectHandle<ObjectHashTable> exports(
            Cast<JSModuleNamespace>(holder)->module()->exports(), isolate());
        InternalIndex entry =
            exports->FindEntry(isolate(), roots, lookup->name(),
                               Smi::ToInt(Object::GetHash(*lookup->name())));
        // We found the accessor, so the entry must exist.
        DCHECK(entry.is_found());
        int value_index = ObjectHashTable::EntryToValueIndex(entry);
        Handle<Smi> smi_handler =
            LoadHandler::LoadModuleExport(isolate(), value_index);
        if (holder_is_lookup_start_object) {
          return MaybeObjectHandle(smi_handler);
        }
        return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
            isolate(), map, holder, *smi_handler));
      }

      Handle<Object> accessors = lookup->GetAccessors();
      if (IsAccessorPair(*accessors)) {
        Handle<AccessorPair> accessor_pair = Cast<AccessorPair>(accessors);
        if (lookup->TryLookupCachedProperty(accessor_pair)) {
          DCHECK_EQ(LookupIterator::DATA, lookup->state());
          return MaybeObjectHandle(ComputeHandler(lookup));
        }

        Handle<Object> getter(accessor_pair->getter(), isolate());
        if (!IsCallableJSFunction(*getter) &&
            !IsFunctionTemplateInfo(*getter)) {
          // TODO(jgruber): Update counter name.
          TRACE_HANDLER_STATS(isolate(), LoadIC_SlowStub);
          return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
        }
        set_accessor(getter);

        if ((IsFunctionTemplateInfo(*getter) &&
             Cast<FunctionTemplateInfo>(*getter)->BreakAtEntry(isolate())) ||
            (IsJSFunction(*getter) &&
             Cast<JSFunction>(*getter)->shared()->BreakAtEntry(isolate()))) {
          // Do not install an IC if the api function has a breakpoint.
          TRACE_HANDLER_STATS(isolate(), LoadIC_SlowStub);
          return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
        }

        Handle<Smi> smi_handler;

        CallOptimization call_optimization(isolate(), getter);
        if (call_optimization.is_simple_api_call()) {
          CallOptimization::HolderLookup holder_lookup;
          Handle<JSObject> api_holder =
              call_optimization.LookupHolderOfExpectedType(isolate(), map,
                                                           &holder_lookup);

          if (!call_optimization.IsCompatibleReceiverMap(api_holder, holder,
                                                         holder_lookup) ||
              !holder->HasFastProperties()) {
            TRACE_HANDLER_STATS(isolate(), LoadIC_SlowStub);
            return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
          }

          smi_handler = LoadHandler::LoadApiGetter(
              isolate(), holder_lookup == CallOptimization::kHolderIsReceiver);

          Handle<NativeContext> accessor_context =
              GetAccessorContext(call_optimization, holder->map(), isolate());

          TRACE_HANDLER_STATS(isolate(), LoadIC_LoadApiGetterFromPrototypeDH);
          return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
              isolate(), map, holder, *smi_handler,
              MaybeObjectHandle::Weak(call_optimization.api_call_info()),
              MaybeObjectHandle::Weak(accessor_context)));
        }

        if (holder->HasFastProperties()) {
          DCHECK(IsCallableJSFunction(*getter));
          if (holder_is_lookup_start_object) {
            TRACE_HANDLER_STATS(isolate(), LoadIC_LoadAccessorDH);
            return MaybeObjectHandle::Weak(accessor_pair);
          }
          TRACE_HANDLER_STATS(isolate(), LoadIC_LoadAccessorFromPrototypeDH);
          return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
              isolate(), map, holder,
              *LoadHandler::LoadAccessorFromPrototype(isolate()),
              MaybeObjectHandle::Weak(getter)));
        }

        if (IsJSGlobalObject(*holder)) {
          TRACE_HANDLER_STATS(isolate(), LoadIC_LoadGlobalFromPrototypeDH);
          smi_handler = LoadHandler::LoadGlobal(isolate());
          return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
              isolate(), map, holder, *smi_handler,
              MaybeObjectHandle::Weak(lookup->GetPropertyCell())));
        } else {
          smi_handler = LoadHandler::LoadNormal(isolate());
          TRACE_HANDLER_STATS(isolate(), LoadIC_LoadNormalDH);
          if (holder_is_lookup_start_object)
            return MaybeObjectHandle(smi_handler);
          TRACE_HANDLER_STATS(isolate(), LoadIC_LoadNormalFromPrototypeDH);
        }

        return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
            isolate(), map, holder, *smi_handler));
      }

      DirectHandle<AccessorInfo> info = Cast<AccessorInfo>(accessors);

      if (info->replace_on_access()) {
        set_slow_stub_reason(
            "getter needs to be reconfigured to data property");
        TRACE_HANDLER_STATS(isolate(), LoadIC_SlowStub);
        return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
      }

      if (!info->has_getter(isolate()) || !holder->HasFastProperties() ||
          (info->is_sloppy() && !IsJSReceiver(*receiver))) {
        TRACE_HANDLER_STATS(isolate(), LoadIC_SlowStub);
        return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
      }

      Handle<Smi> smi_handler = LoadHandler::LoadNativeDataProperty(
          isolate(), lookup->GetAccessorIndex());
      TRACE_HANDLER_STATS(isolate(), LoadIC_LoadNativeDataPropertyDH);
      if (holder_is_lookup_start_object) return MaybeObjectHandle(smi_handler);
      TRACE_HANDLER_STATS(isolate(),
                          LoadIC_LoadNativeDataPropertyFromPrototypeDH);
      return MaybeObjectHandle(
          LoadHandler::LoadFromPrototype(isolate(), map, holder, *smi_handler));
    }

    case LookupIterator::DATA: {
      Handle<JSReceiver> holder = lookup->GetHolder<JSReceiver>();
      DCHECK_EQ(PropertyKind::kData, lookup->property_details().kind());
      Handle<Smi> smi_handler;
      if (lookup->is_dictionary_holder()) {
        if (IsJSGlobalObject(*holder, isolate())) {
          // TODO(verwaest): Also supporting the global object as receiver is a
          // workaround for code that leaks the global object.
          TRACE_HANDLER_STATS(isolate(), LoadIC_LoadGlobalDH);
          smi_handler = LoadHandler::LoadGlobal(isolate());
          return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
              isolate(), map, holder, *smi_handler,
              MaybeObjectHandle::Weak(lookup->GetPropertyCell())));
        }
        smi_handler = LoadHandler::LoadNormal(isolate());
        TRACE_HANDLER_STATS(isolate(), LoadIC_LoadNormalDH);
        if (holder_is_lookup_start_object)
          return MaybeObjectHandle(smi_handler);
        TRACE_HANDLER_STATS(isolate(), LoadIC_LoadNormalFromPrototypeDH);
      } else if (lookup->IsElement(*holder)) {
        TRACE_HANDLER_STATS(isolate(), LoadIC_SlowStub);
        return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
      } else {
        DCHECK_EQ(PropertyLocation::kField,
                  lookup->property_details().location());
        DCHECK(IsJSObject(*holder, isolate()));
        FieldIndex field = lookup->GetFieldIndex();
        smi_handler = LoadHandler::LoadField(isolate(), field);
        TRACE_HANDLER_STATS(isolate(), LoadIC_LoadFieldDH);
        if (holder_is_lookup_start_object)
          return MaybeObjectHandle(smi_handler);
        TRACE_HANDLER_STATS(isolate(), LoadIC_LoadFieldFromPrototypeDH);
      }
      if (lookup->constness() == PropertyConstness::kConst &&
          !holder_is_lookup_start_object) {
        DCHECK_IMPLIES(!V8_DICT_PROPERTY_CONST_TRACKING_BOOL,
                       !lookup->is_dictionary_holder());

        DirectHandle<Object> value = lookup->GetDataValue();

        if (IsThinString(*value)) {
          value = handle(Cast<ThinString>(*value)->actual(), isolate());
        }

        // Non internalized strings could turn into thin/cons strings
        // when internalized. Weak references to thin/cons strings are
        // not supported in the GC. If concurrent marking is running
        // and the thin/cons string is marked but the actual string is
        // not, then the weak reference could be missed.
        if (!IsString(*value) ||
            (IsString(*value) && IsInternalizedString(*value))) {
          MaybeObjectHandle weak_value =
              IsSmi(*value) ? MaybeObjectHandle(*value, isolate())
                            : MaybeObjectHandle::Weak(*value, isolate());

          smi_handler = LoadHandler::LoadConstantFromPrototype(isolate());
          TRACE_HANDLER_STATS(isolate(), LoadIC_LoadConstantFromPrototypeDH);
          return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
              isolate(), map, holder, *smi_handler, weak_value));
        }
      }
      return MaybeObjectHandle(
          LoadHandler::LoadFromPrototype(isolate(), map, holder, *smi_handler));
    }
    case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
      TRACE_HANDLER_STATS(isolate(), LoadIC_LoadIntegerIndexedExoticDH);
      return MaybeObjectHandle(LoadHandler::LoadNonExistent(isolate()));

    case LookupIterator::JSPROXY: {
      // Private names on JSProxy is currently not supported.
      if (lookup->name()->IsPrivate()) {
        return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
      }
      Handle<Smi> smi_handler = LoadHandler::LoadProxy(isolate());
      if (holder_is_lookup_start_object) return MaybeObjectHandle(smi_handler);

      Handle<JSProxy> holder_proxy = lookup->GetHolder<JSProxy>();
      return MaybeObjectHandle(LoadHandler::LoadFromPrototype(
          isolate(), map, holder_proxy, *smi_handler));
    }

    case LookupIterator::WASM_OBJECT:
      return MaybeObjectHandle(LoadHandler::LoadSlow(isolate()));
    case LookupIterator::ACCESS_CHECK:
    case LookupIterator::NOT_FOUND:
    case LookupIterator::TRANSITION:
      UNREACHABLE();
  }

  return MaybeObjectHandle(Handle<InstructionStream>::null());
}

KeyedAccessLoadMode KeyedLoadIC::GetKeyedAccessLoadModeFor(
    DirectHandle<Map> receiver_map) const {
  const MaybeObjectHandle& handler = nexus()->FindHandlerForMap(receiver_map);
  if (handler.is_null()) return KeyedAccessLoadMode::kInBounds;
  return LoadHandler::GetKeyedAccessLoadMode(*handler);
}

// Returns whether the load mode transition is allowed.
bool AllowedHandlerChange(KeyedAccessLoadMode old_mode,
                          KeyedAccessLoadMode new_mode) {
  // Only allow transitions to allow OOB or allow converting a hole to
  // undefined.
  using T = std::underlying_type<KeyedAccessLoadMode>::type;
  return ((static_cast<T>(old_mode) ^
           static_cast<T>(GeneralizeKeyedAccessLoadMode(old_mode, new_mode))) &
          0b11) != 0;
}

void KeyedLoadIC::UpdateLoadElement(Handle<HeapObject> receiver,
                                    const KeyedAccessLoadMode new_load_mode) {
  Handle<Map> receiver_map(receiver->map(), isolate());
  DCHECK(receiver_map->instance_type() !=
         JS_PRIMITIVE_WRAPPER_TYPE);  // Checked by caller.
  MapHandles target_receiver_maps;
  TargetMaps(&target_receiver_maps);

  if (target_receiver_maps.empty()) {
    Handle<Object> handler = LoadElementHandler(receiver_map, new_load_mode);
    return ConfigureVectorState(Handle<Name>(), receiver_map, handler);
  }

  for (Handle<Map> map : target_receiver_maps) {
    if (map.is_null()) continue;
    if (map->instance_type() == JS_PRIMITIVE_WRAPPER_TYPE) {
      set_slow_stub_reason("JSPrimitiveWrapper");
      return;
    }
    if (map->instance_type() == JS_PROXY_TYPE) {
      set_slow_stub_reason("JSProxy");
      return;
    }
  }

  // The first time a receiver is seen that is a transitioned version of the
  // previous monomorphic receiver type, assume the new ElementsKind is the
  // monomorphic type. This benefits global arrays that only transition
  // once, and all call sites accessing them are faster if they remain
  // monomorphic. If this optimistic assumption is not true, the IC will
  // miss again and it will become polymorphic and support both the
  // untransitioned and transitioned maps.
  if (state() == MONOMORPHIC) {
    if ((IsJSObject(*receiver) &&
         IsMoreGeneralElementsKindTransition(
             target_receiver_maps.at(0)->elements_kind(),
             Cast<JSObject>(receiver)->GetElementsKind())) ||
        IsWasmObject(*receiver)) {
      Handle<Object> handler = LoadElementHandler(receiver_map, new_load_mode);
      return ConfigureVectorState(Handle<Name>(), receiver_map, handler);
    }
  }

  DCHECK(state() != GENERIC);

  // Determine the list of receiver maps that this call site has seen,
  // adding the map that was just encountered.
  KeyedAccessLoadMode old_load_mode = KeyedAccessLoadMode::kInBounds;
  if (!AddOneReceiverMapIfMissing(&target_receiver_maps, receiver_map)) {
    old_load_mode = GetKeyedAccessLoadModeFor(receiver_map);
    if (!AllowedHandlerChange(old_load_mode, new_load_mode)) {
      set_slow_stub_reason("same map added twice");
      return;
    }
  }

  // If the maximum number of receiver maps has been exceeded, use the generic
  // version of the IC.
  if (static_cast<int>(target_receiver_maps.size()) >
      v8_flags.max_valid_polymorphic_map_count) {
    set_slow_stub_reason("max polymorph exceeded");
    return;
  }

  MaybeObjectHandles handlers;
  handlers.reserve(target_receiver_maps.size());
  KeyedAccessLoadMode load_mode =
      GeneralizeKeyedAccessLoadMode(old_load_mode, new_load_mode);
  LoadElementPolymorphicHandlers(&target_receiver_maps, &handlers, load_mode);
  if (target_receiver_maps.empty()) {
    Handle<Object> handler = LoadElementHandler(receiver_map, new_load_mode);
    ConfigureVectorState(Handle<Name>(), receiver_map, handler);
  } else if (target_receiver_maps.size() == 1) {
    ConfigureVectorState(Handle<Name>(), target_receiver_maps[0], handlers[0]);
  } else {
    ConfigureVectorState(Handle<Name>(),
                         MapHandlesSpan(target_receiver_maps.begin(),
                                        target_receiver_maps.end()),
                         &handlers);
  }
}

namespace {

bool AllowConvertHoleElementToUndefined(Isolate* isolate,
                                        DirectHandle<Map> receiver_map) {
  if (IsJSTypedArrayMap(*receiver_map)) {
    // For JSTypedArray we never lookup elements in the prototype chain.
    return true;
  }

  // For other {receiver}s we need to check the "no elements" protector.
  if (Protectors::IsNoElementsIntact(isolate)) {
    if (IsStringMap(*receiver_map)) {
      return true;
    }
    if (IsJSObjectMap(*receiver_map)) {
      // For other JSObjects (including JSArrays) we can only continue if
      // the {receiver}s prototype is either the initial Object.prototype
      // or the initial Array.prototype, which are both guarded by the
      // "no elements" protector checked above.
      DirectHandle<Object> receiver_prototype(receiver_map->prototype(),
                                              isolate);

      if (isolate->IsInAnyContext(*receiver_prototype,
                                  Context::INITIAL_ARRAY_PROTOTYPE_INDEX) ||
          isolate->IsInAnyContext(*receiver_prototype,
                                  Context::INITIAL_OBJECT_PROTOTYPE_INDEX)) {
        return true;
      }
    }
  }

  return false;
}

bool IsOutOfBoundsAccess(DirectHandle<Object> receiver, size_t index) {
  size_t length;
  if (IsJSArray(*receiver)) {
    length = Object::NumberValue(Cast<JSArray>(*receiver)->length());
  } else if (IsJSTypedArray(*receiver)) {
    length = Cast<JSTypedArray>(*receiver)->GetLength();
  } else if (IsJSObject(*receiver)) {
    length = Cast<JSObject>(*receiver)->elements()->length();
  } else if (IsString(*receiver)) {
    length = Cast<String>(*receiver)->length();
  } else {
    return false;
  }
  return index >= length;
}

bool AllowReadingHoleElement(ElementsKind elements_kind) {
  return IsHoleyElementsKind(elements_kind);
}

KeyedAccessLoadMode GetNewKeyedLoadMode(Isolate* isolate,
                                        Handle<HeapObject> receiver,
                                        size_t index, bool is_found) {
  DirectHandle<Map> receiver_map(Cast<HeapObject>(receiver)->map(), isolate);
  if (!AllowConvertHoleElementToUndefined(isolate, receiver_map)) {
    return KeyedAccessLoadMode::kInBounds;
  }

  // Always handle holes when the elements kind is HOLEY_ELEMENTS, since the
  // optimizer compilers can not benefit from this information to narrow the
  // type. That is, the load type will always just be a generic tagged value.
  // This avoid an IC miss if we see a hole.
  ElementsKind elements_kind = receiver_map->elements_kind();
  bool always_handle_holes = (elements_kind == HOLEY_ELEMENTS);

  // In bound access and did not read a hole.
  if (is_found) {
    return always_handle_holes ? KeyedAccessLoadMode::kHandleHoles
                               : KeyedAccessLoadMode::kInBounds;
  }

  // OOB access.
  bool is_oob_access = IsOutOfBoundsAccess(receiver, index);
  if (is_oob_access) {
    return always_handle_holes ? KeyedAccessLoadMode::kHandleOOBAndHoles
                               : KeyedAccessLoadMode::kHandleOOB;
  }

  // Read a hole.
  DCHECK(!is_found && !is_oob_access);
  bool handle_hole = AllowReadingHoleElement(elements_kind);
  DCHECK_IMPLIES(always_handle_holes, handle_hole);
  return handle_hole ? KeyedAccessLoadMode::kHandleHoles
                     : KeyedAccessLoadMode::kInBounds;
}

KeyedAccessLoadMode GetUpdatedLoadModeForMap(Isolate* isolate,
                                             DirectHandle<Map> map,
                                             KeyedAccessLoadMode load_mode) {
  // If we are not allowed to convert a hole to undefined, then we should not
  // handle OOB nor reading holes.
  if (!AllowConvertHoleElementToUndefined(isolate, map)) {
    return KeyedAccessLoadMode::kInBounds;
  }
  // Check if the elements kind allow reading a hole.
  bool allow_reading_hole_element =
      AllowReadingHoleElement(map->elements_kind());
  switch (load_mode) {
    case KeyedAccessLoadMode::kInBounds:
    case KeyedAccessLoadMode::kHandleOOB:
      return load_mode;
    case KeyedAccessLoadMode::kHandleHoles:
      return allow_reading_hole_element ? KeyedAccessLoadMode::kHandleHoles
                                        : KeyedAccessLoadMode::kInBounds;
    case KeyedAccessLoadMode::kHandleOOBAndHoles:
      return allow_reading_hole_element
                 ? KeyedAccessLoadMode::kHandleOOBAndHoles
                 : KeyedAccessLoadMode::kHandleOOB;
  }
}

}  // namespace

Handle<Object> KeyedLoadIC::LoadElementHandler(
    DirectHandle<Map> receiver_map, KeyedAccessLoadMode new_load_mode) {
  // Has a getter interceptor, or is any has and has a query interceptor.
  if (receiver_map->has_indexed_interceptor() &&
      (!IsUndefined(receiver_map->GetIndexedInterceptor()->getter(),
                    isolate()) ||
       (IsAnyHas() &&
        !IsUndefined(receiver_map->GetIndexedInterceptor()->query(),
                     isolate()))) &&
      !receiver_map->GetIndexedInterceptor()->non_masking()) {
    // TODO(jgruber): Update counter name.
    TRACE_HANDLER_STATS(isolate(), KeyedLoadIC_LoadIndexedInterceptorStub);
    return IsAnyHas() ? BUILTIN_CODE(isolate(), HasIndexedInterceptorIC)
                      : BUILTIN_CODE(isolate(), LoadIndexedInterceptorIC);
  }

  InstanceType instance_type = receiver_map->instance_type();
  if (instance_type < FIRST_NONSTRING_TYPE) {
    TRACE_HANDLER_STATS(isolate(), KeyedLoadIC_LoadIndexedStringDH);
    if (IsAnyHas()) return LoadHandler::LoadSlow(isolate());
    return LoadHandler::LoadIndexedString(isolate(), new_load_mode);
  }
  if (instance_type < FIRST_JS_RECEIVER_TYPE) {
    TRACE_HANDLER_STATS(isolate(), KeyedLoadIC_SlowStub);
    return LoadHandler::LoadSlow(isolate());
  }
  if (instance_type == JS_PROXY_TYPE) {
    return LoadHandler::LoadProxy(isolate());
  }
#if V8_ENABLE_WEBASSEMBLY
  if (InstanceTypeChecker::IsWasmObject(instance_type)) {
    // TODO(jgruber): Update counter name.
    TRACE_HANDLER_STATS(isolate(), KeyedLoadIC_SlowStub);
    return LoadHandler::LoadSlow(isolate());
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  ElementsKind elements_kind = receiver_map->elements_kind();
  if (IsSloppyArgumentsElementsKind(elements_kind)) {
    // TODO(jgruber): Update counter name.
    TRACE_HANDLER_STATS(isolate(), KeyedLoadIC_KeyedLoadSloppyArgumentsStub);
    return IsAnyHas() ? BUILTIN_CODE(isolate(), KeyedHasIC_SloppyArguments)
                      : BUILTIN_CODE(isolate(), KeyedLoadIC_SloppyArguments);
  }
  bool is_js_array = instance_type == JS_ARRAY_TYPE;
  if (elements_kind == DICTIONARY_ELEMENTS) {
    TRACE_HANDLER_STATS(isolate(), KeyedLoadIC_LoadElementDH);
    return LoadHandler::LoadElement(isolate(), elements_kind, is_js_array,
                                    new_load_mode);
  }
  DCHECK(IsFastElementsKind(elements_kind) ||
         IsAnyNonextensibleElementsKind(elements_kind) ||
         IsTypedArrayOrRabGsabTypedArrayElementsKind(elements_kind));
  DCHECK_IMPLIES(
      LoadModeHandlesHoles(new_load_mode),
      AllowReadingHoleElement(elements_kind) &&
          AllowConvertHoleElementToUndefined(isolate(), receiver_map));
  TRACE_HANDLER_STATS(isolate(), KeyedLoadIC_LoadElementDH);
  return LoadHandler::LoadElement(isolate(), elements_kind, is_js_array,
                                  new_load_mode);
}

void KeyedLoadIC::LoadElementPolymorphicHandlers(
    MapHandles* receiver_maps, MaybeObjectHandles* handlers,
    KeyedAccessLoadMode new_load_mode) {
  // Filter out deprecated maps to ensure their instances get migrated.
  receiver_maps->erase(
      std::remove_if(
          receiver_maps->begin(), receiver_maps->end(),
          [](const DirectHandle<Map>& map) { return map->is_deprecated(); }),
      receiver_maps->end());

  for (DirectHandle<Map> receiver_map : *receiver_maps) {
    // Mark all stable receiver maps that have elements kind transition map
    // among receiver_maps as unstable because the optimizing compilers may
    // generate an elements kind transition for this kind of receivers.
    if (receiver_map->is_stable()) {
      Tagged<Map> tmap = receiver_map->FindElementsKindTransitionedMap(
          isolate(),
          MapHandlesSpan(receiver_maps->begin(), receiver_maps->end()),
          ConcurrencyMode::kSynchronous);
      if (!tmap.is_null()) {
        receiver_map->NotifyLeafMapLayoutChange(isolate());
      }
    }
    handlers->push_back(MaybeObjectHandle(LoadElementHandler(
        receiver_map,
        GetUpdatedLoadModeForMap(isolate(), receiver_map, new_load_mode))));
  }
}

namespace {

enum KeyType { kIntPtr, kName, kBailout };

// The cases where kIntPtr is returned must match what
// CodeStubAssembler::TryToIntptr can handle!
KeyType TryConvertKey(Handle<Object> key, Isolate* isolate, intptr_t* index_out,
                      Handle<Name>* name_out) {
  if (IsSmi(*key)) {
    *index_out = Smi::ToInt(*key);
    return kIntPtr;
  }
  if (IsHeapNumber(*key)) {
    double num = Cast<HeapNumber>(*key)->value();
    if (!(num >= -kMaxSafeInteger)) return kBailout;
    if (num > kMaxSafeInteger) return kBailout;
    *index_out = static_cast<intptr_t>(num);
    if (*index_out != num) return kBailout;
    return kIntPtr;
  }
  if (IsString(*key)) {
    key = isolate->factory()->InternalizeString(Cast<String>(key));
    uint32_t maybe_array_index;
    if (Cast<String>(*key)->AsArrayIndex(&maybe_array_index)) {
      if (maybe_array_index <= INT_MAX) {
        *index_out = static_cast<intptr_t>(maybe_array_index);
        return kIntPtr;
      }
      // {key} is a string representation of an array index beyond the range
      // that the IC could handle. Don't try to take the named-property path.
      return kBailout;
    }
    *name_out = Cast<String>(key);
    return kName;
  }
  if (IsSymbol(*key)) {
    *name_out = Cast<Symbol>(key);
    return kName;
  }
  return kBailout;
}

bool IntPtrKeyToSize(intptr_t index, DirectHandle<HeapObject> receiver,
                     size_t* out) {
  if (index < 0) {
    if (IsJSTypedArray(*receiver)) {
      // For JSTypedArray receivers, we can support negative keys, which we
      // just map to a very large value. This is valid because all OOB accesses
      // (negative or positive) are handled the same way, and size_t::max is
      // guaranteed to be an OOB access.
      *out = std::numeric_limits<size_t>::max();
      return true;
    }
    return false;
  }
#if V8_HOST_ARCH_64_BIT
  if (index > JSObject::kMaxElementIndex && !IsJSTypedArray(*receiver)) {
    return false;
  }
#else
  // On 32-bit platforms, any intptr_t is less than kMaxElementIndex.
  static_assert(
      static_cast<double>(std::numeric_limits<decltype(index)>::max()) <=
      static_cast<double>(JSObject::kMaxElementIndex));
#endif
  *out = static_cast<size_t>(index);
  return true;
}

bool CanCache(DirectHandle<Object> receiver, InlineCacheState state) {
  if (!v8_flags.use_ic || state == NO_FEEDBACK) return false;
  if (!IsJSReceiver(*receiver) && !IsString(*receiver)) return false;
  return !IsAccessCheckNeeded(*receiver) && !IsJSPrimitiveWrapper(*receiver);
}

}  // namespace

MaybeHandle<Object> KeyedLoadIC::RuntimeLoad(Handle<Object> object,
                                             Handle<Object> key,
                                             bool* is_found) {
  Handle<Object> result;

  if (IsKeyedLoadIC()) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate(), result,
        Runtime::GetObjectProperty(isolate(), object, key, Handle<Object>(),
                                   is_found));
  } else {
    DCHECK(IsKeyedHasIC());
    ASSIGN_RETURN_ON_EXCEPTION(isolate(), result,
                               Runtime::HasProperty(isolate(), object, key));
  }
  return result;
}

MaybeHandle<Object> KeyedLoadIC::LoadName(Handle<Object> object,
                                          DirectHandle<Object> key,
                                          Handle<Name> name) {
  Handle<Object> load_handle;
  ASSIGN_RETURN_ON_EXCEPTION(isolate(), load_handle,
                             LoadIC::Load(object, name));

  if (vector_needs_update()) {
    ConfigureVectorState(MEGAMORPHIC, key);
    TraceIC("LoadIC", key);
  }

  DCHECK(!load_handle.is_null());
  return load_handle;
}

MaybeHandle<Object> KeyedLoadIC::Load(Handle<Object> object,
                                      Handle<Object> key) {
  if (MigrateDeprecated(isolate(), object)) {
    return RuntimeLoad(object, key);
  }

  intptr_t maybe_index;
  Handle<Name> maybe_name;
  KeyType key_type = TryConvertKey(key, isolate(), &maybe_index, &maybe_name);

  if (key_type == kName) return LoadName(object, key, maybe_name);

  bool is_found = false;
  MaybeHandle<Object> result = RuntimeLoad(object, key, &is_found);

  size_t index;
  if (key_type == kIntPtr && CanCache(object, state()) &&
      IntPtrKeyToSize(maybe_index, Cast<HeapObject>(object), &index)) {
    Handle<HeapObject> receiver = Cast<HeapObject>(object);
    KeyedAccessLoadMode load_mode =
        GetNewKeyedLoadMode(isolate(), receiver, index, is_found);
    UpdateLoadElement(receiver, load_mode);
    if (is_vector_set()) {
      TraceIC("LoadIC", key);
    }
  }

  if (vector_needs_update()) {
    ConfigureVectorState(MEGAMORPHIC, key);
    TraceIC("LoadIC", key);
  }

  return result;
}

bool StoreIC::LookupForWrite(LookupIterator* it, DirectHandle<Object> value,
                             StoreOrigin store_origin) {
  // Disable ICs for non-JSObjects for now.
  Handle<Object> object = it->GetReceiver();
  if (IsJSProxy(*object)) return true;
  if (!IsJSObject(*object)) return false;
  Handle<JSObject> receiver = Cast<JSObject>(object);
  DCHECK(!receiver->map()->is_deprecated());

  for (;; it->Next()) {
    switch (it->state()) {
      case LookupIterator::TRANSITION:
        UNREACHABLE();
      case LookupIterator::WASM_OBJECT:
        return false;
      case LookupIterator::JSPROXY:
        return true;
      case LookupIterator::INTERCEPTOR: {
        DirectHandle<JSObject> holder = it->GetHolder<JSObject>();
        Tagged<InterceptorInfo> info = holder->GetNamedInterceptor();
        if (it->HolderIsReceiverOrHiddenPrototype() ||
            !IsUndefined(info->getter(), isolate()) ||
            !IsUndefined(info->query(), isolate())) {
          return true;
        }
        continue;
      }
      case LookupIterator::ACCESS_CHECK:
        if (IsAccessCheckNeeded(*it->GetHolder<JSObject>())) return false;
        continue;
      case LookupIterator::ACCESSOR:
        return !it->IsReadOnly();
      case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
        return false;
      case LookupIterator::DATA: {
        if (it->IsReadOnly()) return false;
        if (IsAnyDefineOwn() && it->property_attributes() != NONE) {
          // IC doesn't support reconfiguration of property attributes,
          // so just bail out to the slow handler.
          return false;
        }
        Handle<JSObject> holder = it->GetHolder<JSObject>();
        if (receiver.is_identical_to(holder)) {
          it->PrepareForDataProperty(value);
          // The previous receiver map might just have been deprecated,
          // so reload it.
          update_lookup_start_object_map(receiver);
          return true;
        }

        // Receiver != holder.
        if (IsJSGlobalProxy(*receiver)) {
          PrototypeIterator iter(isolate(), receiver);
          return it->GetHolder<Object>().is_identical_to(
              PrototypeIterator::GetCurrent(iter));
        }

        if (it->HolderIsReceiverOrHiddenPrototype()) return false;

        if (it->ExtendingNonExtensible(receiver)) return false;
        it->PrepareTransitionToDataProperty(receiver, value, NONE,
                                            store_origin);
        return it->IsCacheableTransition();
      }
      case LookupIterator::NOT_FOUND:
        // If we are in StoreGlobal then check if we should throw on
        // non-existent properties.
        if (IsStoreGlobalIC() &&
            (GetShouldThrow(it->isolate(), Nothing<ShouldThrow>()) ==
             ShouldThrow::kThrowOnError)) {
          // ICs typically does the store in two steps: prepare receiver for the
          // transition followed by the actual store. For global objects we
          // create a property cell when preparing for transition and install
          // this cell in the handler. In strict mode, we throw and never
          // initialize this property cell. The IC handler assumes that the
          // property cell it is holding is for a property that is existing.
          // This case violates this assumption. If we happen to invalidate this
          // property cell later, it leads to incorrect behaviour. For now just
          // use a slow stub and don't install the property cell for these
          // cases. Hopefully these cases are not frequent enough to impact
          // performance.
          //
          // TODO(mythria): If we find this to be happening often, we could
          // install a new kind of handler for non-existent properties. These
          // handlers can then miss to runtime if the value is not hole (i.e.
          // cell got invalidated) and handle these stores correctly.
          return false;
        }
        receiver = it->GetStoreTarget<JSObject>();
        if (it->ExtendingNonExtensible(receiver)) return false;
        it->PrepareTransitionToDataProperty(receiver, value, NONE,
                                            store_origin);
        return it->IsCacheableTransition();
    }
    UNREACHABLE();
  }
}

MaybeHandle<Object> StoreGlobalIC::Store(Handle<Name> name,
                                         Handle<Object> value) {
  DCHECK(IsString(*name));

  // Look up in script context table.
  Handle<String> str_name = Cast<String>(name);
  Handle<JSGlobalObject> global = isolate()->global_object();
  DirectHandle<ScriptContextTable> script_contexts(
      global->native_context()->script_context_table(), isolate());

  VariableLookupResult lookup_result;
  if (script_contexts->Lookup(str_name, &lookup_result)) {
    DisallowGarbageCollection no_gc;
    DisableGCMole no_gcmole;
    Tagged<Context> script_context =
        script_contexts->get(lookup_result.context_index);
    if (IsImmutableLexicalVariableMode(lookup_result.mode)) {
      AllowGarbageCollection yes_gc;
      return TypeError(MessageTemplate::kConstAssign, global, name);
    }

    Tagged<Object> previous_value =
        script_context->get(lookup_result.slot_index);

    if (IsTheHole(previous_value, isolate())) {
      // Do not install stubs and stay pre-monomorphic for uninitialized
      // accesses.
      AllowGarbageCollection yes_gc;
      THROW_NEW_ERROR(
          isolate(),
          NewReferenceError(MessageTemplate::kAccessedUninitializedVariable,
                            name));
    }

    if (lookup_result.mode == VariableMode::kLet &&
        v8_flags.const_tracking_let) {
      Context::UpdateConstTrackingLetSideData(handle(script_context, isolate()),
                                              lookup_result.slot_index, value,
                                              isolate());
    }

    bool use_ic = (state() != NO_FEEDBACK) && v8_flags.use_ic;
    if (use_ic) {
      if (nexus()->ConfigureLexicalVarMode(
              lookup_result.context_index, lookup_result.slot_index,
              IsImmutableLexicalVariableMode(lookup_result.mode))) {
        TRACE_HANDLER_STATS(isolate(), StoreGlobalIC_StoreScriptContextField);
      } else {
        // Given combination of indices can't be encoded, so use slow stub.
        TRACE_HANDLER_STATS(isolate(), StoreGlobalIC_SlowStub);
        SetCache(name, StoreHandler::StoreSlow(isolate()));
      }
      TraceIC("StoreGlobalIC", name);
    } else if (state() == NO_FEEDBACK) {
      TraceIC("StoreGlobalIC", name);
    }
    script_context->set(lookup_result.slot_index, *value);
    return value;
  }

  return StoreIC::Store(global, name, value);
}

namespace {
Maybe<bool> DefineOwnDataProperty(LookupIterator* it,
                                  LookupIterator::State original_state,
                                  Handle<JSAny> value,
                                  Maybe<ShouldThrow> should_throw,
                                  StoreOrigin store_origin) {
  // It should not be possible to call DefineOwnDataProperty in a
  // contextual store (indicated by IsJSGlobalObject()).
  DCHECK(!IsJSGlobalObject(*it->GetReceiver(), it->isolate()));

  // Handle special cases that can't be handled by
  // DefineOwnPropertyIgnoreAttributes first.
  switch (it->state()) {
    case LookupIterator::JSPROXY: {
      PropertyDescriptor new_desc;
      new_desc.set_value(value);
      new_desc.set_writable(true);
      new_desc.set_enumerable(true);
      new_desc.set_configurable(true);
      DCHECK_EQ(original_state, LookupIterator::JSPROXY);
      // TODO(joyee): this will start the lookup again. Ideally we should
      // implement something that reuses the existing LookupIterator.
      return JSProxy::DefineOwnProperty(it->isolate(), it->GetHolder<JSProxy>(),
                                        it->GetName(), &new_desc, should_throw);
    }
    case LookupIterator::WASM_OBJECT:
      RETURN_FAILURE(it->isolate(), kThrowOnError,
                     NewTypeError(MessageTemplate::kWasmObjectsAreOpaque));
    // When lazy feedback is disabled, the original state could be different
    // while the object is already prepared for TRANSITION.
    case LookupIterator::TRANSITION: {
      switch (original_state) {
        case LookupIterator::JSPROXY:
        case LookupIterator::WASM_OBJECT:
        case LookupIterator::TRANSITION:
        case LookupIterator::DATA:
        case LookupIterator::INTERCEPTOR:
        case LookupIterator::ACCESSOR:
        case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
          UNREACHABLE();
        case LookupIterator::ACCESS_CHECK: {
          DCHECK(!IsAccessCheckNeeded(*it->GetHolder<JSObject>()));
          [[fallthrough]];
        }
        case LookupIterator::NOT_FOUND:
          return Object::AddDataProperty(it, value, NONE,
                                         Nothing<ShouldThrow>(), store_origin,
                                         EnforceDefineSemantics::kDefine);
      }
    }
    case LookupIterator::ACCESS_CHECK:
    case LookupIterator::NOT_FOUND:
    case LookupIterator::DATA:
    case LookupIterator::ACCESSOR:
    case LookupIterator::INTERCEPTOR:
    case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
      break;
  }

  // We need to restart to handle interceptors properly.
  it->Restart();

  return JSObject::DefineOwnPropertyIgnoreAttributes(
      it, value, NONE, should_throw, JSObject::DONT_FORCE_FIELD,
      EnforceDefineSemantics::kDefine, store_origin);
}
}  // namespace

MaybeHandle<Object> StoreIC::Store(Handle<Object> object, Handle<Name> name,
                                   Handle<Object> value,
                                   StoreOrigin store_origin) {
  // TODO(verwaest): Let SetProperty do the migration, since storing a property
  // might deprecate the current map again, if value does not fit.
  if (MigrateDeprecated(isolate(), object)) {
    // KeyedStoreIC should handle DefineKeyedOwnIC with deprecated maps directly
    // instead of reusing this method.
    DCHECK(!IsDefineKeyedOwnIC());
    DCHECK(!name->IsPrivateName());

    PropertyKey key(isolate(), name);
    if (IsDefineNamedOwnIC()) {
      MAYBE_RETURN_NULL(JSReceiver::CreateDataProperty(
          isolate(), object, key, value, Nothing<ShouldThrow>()));
    } else {
      LookupIterator it(isolate(), object, key, LookupIterator::DEFAULT);
      MAYBE_RETURN_NULL(Object::SetProperty(&it, value, StoreOrigin::kNamed));
    }
    return value;
  }

  bool use_ic = (state() != NO_FEEDBACK) && v8_flags.use_ic;
  // If the object is undefined or null it's illegal to try to set any
  // properties on it; throw a TypeError in that case.
  if (IsNullOrUndefined(*object, isolate())) {
    if (use_ic) {
      // Ensure the IC state progresses.
      TRACE_HANDLER_STATS(isolate(), StoreIC_NonReceiver);
      update_lookup_start_object_map(object);
      SetCache(name, StoreHandler::StoreSlow(isolate()));
      TraceIC("StoreIC", name);
    }
    return TypeError(MessageTemplate::kNonObjectPropertyStoreWithProperty, name,
                     object);
  }

  JSObject::MakePrototypesFast(object, kStartAtPrototype, isolate());
  PropertyKey key(isolate(), name);
  LookupIterator it(
      isolate(), object, key,
      IsAnyDefineOwn() ? LookupIterator::OWN : LookupIterator::DEFAULT);

  if (name->IsPrivate()) {
    if (name->IsPrivateName()) {
      DCHECK(!IsDefineNamedOwnIC());
      Maybe<bool> can_store =
          JSReceiver::CheckPrivateNameStore(&it, IsDefineKeyedOwnIC());
      MAYBE_RETURN_NULL(can_store);
      if (!can_store.FromJust()) {
        return isolate()->factory()->undefined_value();
      }
    }

    // IC handling of private fields/symbols stores on JSProxy is not
    // supported.
    if (IsJSProxy(*object)) {
      use_ic = false;
    }
  }

  // For IsAnyDefineOwn(), we can't simply do CreateDataProperty below
  // because we need to check the attributes before UpdateCaches updates
  // the state of the LookupIterator.
  LookupIterator::State original_state = it.state();
  // We'll defer the check for JSProxy and objects with named interceptors,
  // because the defineProperty traps need to be called first if they are
  // present. We can also skip this for private names since they are not
  // bound by configurability or extensibility checks, and errors would've
  // been thrown if the private field already exists in the object.
  if (IsAnyDefineOwn() && !name->IsPrivateName() && IsJSObject(*object) &&
      !Cast<JSObject>(object)->HasNamedInterceptor()) {
    Maybe<bool> can_define = JSObject::CheckIfCanDefineAsConfigurable(
        isolate(), &it, value, Nothing<ShouldThrow>());
    MAYBE_RETURN_NULL(can_define);
    if (!can_define.FromJust()) {
      return isolate()->factory()->undefined_value();
    }
    // Restart the lookup iterator updated by CheckIfCanDefineAsConfigurable()
    // for UpdateCaches() to handle access checks.
    if (use_ic && IsAccessCheckNeeded(*object)) {
      it.Restart();
    }
  }

  if (use_ic) {
    UpdateCaches(&it, value, store_origin);
  } else if (state() == NO_FEEDBACK) {
    // Tracing IC Stats for No Feedback State.
    IsStoreGlobalIC() ? TraceIC("StoreGlobalIC", name)
                      : TraceIC("StoreIC", name);
  }

  // TODO(v8:12548): refactor DefinedNamedOwnIC and SetNamedIC as subclasses
  // of StoreIC so their logic doesn't get mixed here.
  // ES #sec-definefield
  // ES #sec-runtime-semantics-propertydefinitionevaluation
  // IsAnyDefineOwn() can be true when this method is reused by KeyedStoreIC.
  if (IsAnyDefineOwn()) {
    if (name->IsPrivateName()) {
      // We should define private fields without triggering traps or checking
      // extensibility.
      MAYBE_RETURN_NULL(
          JSReceiver::AddPrivateField(&it, value, Nothing<ShouldThrow>()));
    } else {
      MAYBE_RETURN_NULL(
          DefineOwnDataProperty(&it, original_state, Cast<JSAny>(value),
                                Nothing<ShouldThrow>(), store_origin));
    }
  } else {
    MAYBE_RETURN_NULL(Object::SetProperty(&it, value, store_origin));
  }
  return value;
}

void StoreIC::UpdateCaches(LookupIterator* lookup, DirectHandle<Object> value,
                           StoreOrigin store_origin) {
  MaybeObjectHandle handler;
  if (LookupForWrite(lookup, value, store_origin)) {
    if (IsStoreGlobalIC()) {
      if (lookup->state() == LookupIterator::DATA &&
          lookup->GetReceiver().is_identical_to(lookup->GetHolder<Object>())) {
        DCHECK(IsJSGlobalObject(*lookup->GetReceiver()));
        // Now update the cell in the feedback vector.
        nexus()->ConfigurePropertyCellMode(lookup->GetPropertyCell());
        TraceIC("StoreGlobalIC", lookup->GetName());
        return;
      }
    }
    handler = ComputeHandler(lookup);
  } else {
    set_slow_stub_reason("LookupForWrite said 'false'");
    handler = MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
  }
  // Can't use {lookup->name()} because the LookupIterator might be in
  // "elements" mode for keys that are strings representing integers above
  // JSArray::kMaxIndex.
  SetCache(lookup->GetName(), handler);
  TraceIC("StoreIC", lookup->GetName());
}

MaybeObjectHandle StoreIC::ComputeHandler(LookupIterator* lookup) {
  switch (lookup->state()) {
    case LookupIterator::TRANSITION: {
      Handle<JSObject> store_target = lookup->GetStoreTarget<JSObject>();
      if (IsJSGlobalObject(*store_target)) {
        TRACE_HANDLER_STATS(isolate(), StoreIC_StoreGlobalTransitionDH);

        if (IsJSGlobalObject(*lookup_start_object_map())) {
          DCHECK(IsStoreGlobalIC());
#ifdef DEBUG
          DirectHandle<JSObject> holder = lookup->GetHolder<JSObject>();
          DCHECK_EQ(*lookup->GetReceiver(), *holder);
          DCHECK_EQ(*store_target, *holder);
#endif
          return StoreHandler::StoreGlobal(lookup->transition_cell());
        }
        if (IsDefineKeyedOwnIC()) {
          // Private field can't be deleted from this global object and can't
          // be overwritten, so install slow handler in order to make store IC
          // throw if a private name already exists.
          TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
          return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        }

        DirectHandle<Smi> smi_handler =
            StoreHandler::StoreGlobalProxy(isolate());
        Handle<Object> handler = StoreHandler::StoreThroughPrototype(
            isolate(), lookup_start_object_map(), store_target, *smi_handler,
            MaybeObjectHandle::Weak(lookup->transition_cell()));
        return MaybeObjectHandle(handler);
      }
      // Dictionary-to-fast transitions are not expected and not supported.
      DCHECK_IMPLIES(!lookup->transition_map()->is_dictionary_map(),
                     !lookup_start_object_map()->is_dictionary_map());

      DCHECK(lookup->IsCacheableTransition());
      if (IsAnyDefineOwn()) {
        return StoreHandler::StoreOwnTransition(isolate(),
                                                lookup->transition_map());
      }
      return StoreHandler::StoreTransition(isolate(), lookup->transition_map());
    }

    case LookupIterator::INTERCEPTOR: {
      Handle<JSObject> holder = lookup->GetHolder<JSObject>();
      Tagged<InterceptorInfo> info = holder->GetNamedInterceptor();

      // If the interceptor is on the receiver...
      if (lookup->HolderIsReceiverOrHiddenPrototype() && !info->non_masking()) {
        // ...return a store interceptor Smi handler if there is a setter
        // interceptor and it's not DefineNamedOwnIC or DefineKeyedOwnIC
        // (which should call the definer)...
        if (!IsUndefined(info->setter(), isolate()) && !IsAnyDefineOwn()) {
          return MaybeObjectHandle(StoreHandler::StoreInterceptor(isolate()));
        }
        // ...otherwise return a slow-case Smi handler, which invokes the
        // definer for DefineNamedOwnIC.
        return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
      }

      // If the interceptor is a getter/query interceptor on the prototype
      // chain, return an invalidatable slow handler so it can turn fast if the
      // interceptor is masked by a regular property later.
      DCHECK(!IsUndefined(info->getter(), isolate()) ||
             !IsUndefined(info->query(), isolate()));
      Handle<Object> handler = StoreHandler::StoreThroughPrototype(
          isolate(), lookup_start_object_map(), holder,
          *StoreHandler::StoreSlow(isolate()));
      return MaybeObjectHandle(handler);
    }

    case LookupIterator::ACCESSOR: {
      // This is currently guaranteed by checks in StoreIC::Store.
      Handle<JSObject> receiver = Cast<JSObject>(lookup->GetReceiver());
      Handle<JSObject> holder = lookup->GetHolder<JSObject>();
      DCHECK(!IsAccessCheckNeeded(*receiver) || lookup->name()->IsPrivate());

      if (IsAnyDefineOwn()) {
        set_slow_stub_reason("define own with existing accessor");
        TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
        return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
      }
      if (!holder->HasFastProperties()) {
        set_slow_stub_reason("accessor on slow map");
        TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
        MaybeObjectHandle handler =
            MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        return handler;
      }
      Handle<Object> accessors = lookup->GetAccessors();
      if (IsAccessorInfo(*accessors)) {
        DirectHandle<AccessorInfo> info = Cast<AccessorInfo>(accessors);
        if (!info->has_setter(isolate())) {
          set_slow_stub_reason("setter == kNullAddress");
          TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
          return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        }
        if (!lookup->HolderIsReceiverOrHiddenPrototype()) {
          set_slow_stub_reason("native data property in prototype chain");
          TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
          return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        }

        Handle<Smi> smi_handler = StoreHandler::StoreNativeDataProperty(
            isolate(), lookup->GetAccessorIndex());
        TRACE_HANDLER_STATS(isolate(), StoreIC_StoreNativeDataPropertyDH);
        if (receiver.is_identical_to(holder)) {
          return MaybeObjectHandle(smi_handler);
        }
        TRACE_HANDLER_STATS(isolate(),
                            StoreIC_StoreNativeDataPropertyOnPrototypeDH);
        return MaybeObjectHandle(StoreHandler::StoreThroughPrototype(
            isolate(), lookup_start_object_map(), holder, *smi_handler));

      } else if (IsAccessorPair(*accessors)) {
        Handle<AccessorPair> accessor_pair = Cast<AccessorPair>(accessors);
        Handle<Object> setter(accessor_pair->setter(), isolate());
        if (!IsCallableJSFunction(*setter) &&
            !IsFunctionTemplateInfo(*setter)) {
          set_slow_stub_reason("setter not a function");
          TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
          return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        }

        if ((IsFunctionTemplateInfo(*setter) &&
             Cast<FunctionTemplateInfo>(*setter)->BreakAtEntry(isolate())) ||
            (IsJSFunction(*setter) &&
             Cast<JSFunction>(*setter)->shared()->BreakAtEntry(isolate()))) {
          // Do not install an IC if the api function has a breakpoint.
          TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
          return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        }

        CallOptimization call_optimization(isolate(), setter);
        if (call_optimization.is_simple_api_call()) {
          CallOptimization::HolderLookup holder_lookup;
          Handle<JSObject> api_holder =
              call_optimization.LookupHolderOfExpectedType(
                  isolate(), lookup_start_object_map(), &holder_lookup);
          if (call_optimization.IsCompatibleReceiverMap(api_holder, holder,
                                                        holder_lookup)) {
            DirectHandle<Smi> smi_handler = StoreHandler::StoreApiSetter(
                isolate(),
                holder_lookup == CallOptimization::kHolderIsReceiver);

            Handle<NativeContext> accessor_context =
                GetAccessorContext(call_optimization, holder->map(), isolate());

            TRACE_HANDLER_STATS(isolate(), StoreIC_StoreApiSetterOnPrototypeDH);
            return MaybeObjectHandle(StoreHandler::StoreThroughPrototype(
                isolate(), lookup_start_object_map(), holder, *smi_handler,
                MaybeObjectHandle::Weak(call_optimization.api_call_info()),
                MaybeObjectHandle::Weak(accessor_context)));
          }
          set_slow_stub_reason("incompatible receiver");
          TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
          return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        } else if (IsFunctionTemplateInfo(*setter)) {
          set_slow_stub_reason("setter non-simple template");
          TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
          return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
        }

        DCHECK(IsCallableJSFunction(*setter));
        if (receiver.is_identical_to(holder)) {
          TRACE_HANDLER_STATS(isolate(), StoreIC_StoreAccessorDH);
          return MaybeObjectHandle::Weak(accessor_pair);
        }
        TRACE_HANDLER_STATS(isolate(), StoreIC_StoreAccessorOnPrototypeDH);

        return MaybeObjectHandle(StoreHandler::StoreThroughPrototype(
            isolate(), lookup_start_object_map(), holder,
            *StoreHandler::StoreAccessorFromPrototype(isolate()),
            MaybeObjectHandle::Weak(setter)));
      }
      TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
      return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
    }

    case LookupIterator::DATA: {
      // This is currently guaranteed by checks in StoreIC::Store.
      Handle<JSObject> receiver = Cast<JSObject>(lookup->GetReceiver());
      USE(receiver);
      Handle<JSObject> holder = lookup->GetHolder<JSObject>();
      DCHECK(!IsAccessCheckNeeded(*receiver) || lookup->name()->IsPrivate());

      DCHECK_EQ(PropertyKind::kData, lookup->property_details().kind());
      if (lookup->is_dictionary_holder()) {
        if (IsJSGlobalObject(*holder)) {
          TRACE_HANDLER_STATS(isolate(), StoreIC_StoreGlobalDH);
          return MaybeObjectHandle(
              StoreHandler::StoreGlobal(lookup->GetPropertyCell()));
        }
        TRACE_HANDLER_STATS(isolate(), StoreIC_StoreNormalDH);
        DCHECK(holder.is_identical_to(receiver));
        DCHECK_IMPLIES(!V8_DICT_PROPERTY_CONST_TRACKING_BOOL,
                       lookup->constness() == PropertyConstness::kMutable);

        Handle<Smi> handler = StoreHandler::StoreNormal(isolate());
        return MaybeObjectHandle(handler);
      }

      // -------------- Elements (for TypedArrays) -------------
      if (lookup->IsElement(*holder)) {
        TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
        return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
      }

      // -------------- Fields --------------
      if (lookup->property_details().location() == PropertyLocation::kField) {
        TRACE_HANDLER_STATS(isolate(), StoreIC_StoreFieldDH);
        int descriptor = lookup->GetFieldDescriptorIndex();
        FieldIndex index = lookup->GetFieldIndex();
        if (V8_UNLIKELY(IsJSSharedStruct(*holder))) {
          return MaybeObjectHandle(StoreHandler::StoreSharedStructField(
              isolate(), descriptor, index, lookup->representation()));
        }
        PropertyConstness constness = lookup->constness();
        if (constness == PropertyConstness::kConst &&
            IsDefineNamedOwnICKind(nexus()->kind())) {
          // DefineNamedOwnICs are used for initializing object literals
          // therefore we must store the value unconditionally even to
          // VariableMode::kConst fields.
          constness = PropertyConstness::kMutable;
        }
        return MaybeObjectHandle(StoreHandler::StoreField(
            isolate(), descriptor, index, constness, lookup->representation()));
      }

      // -------------- Constant properties --------------
      DCHECK_EQ(PropertyLocation::kDescriptor,
                lookup->property_details().location());
      set_slow_stub_reason("constant property");
      TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
      return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
    }
    case LookupIterator::JSPROXY: {
      Handle<JSReceiver> receiver = Cast<JSReceiver>(lookup->GetReceiver());
      Handle<JSProxy> holder = lookup->GetHolder<JSProxy>();

      // IsDefineNamedOwnIC() is true when we are defining public fields on a
      // Proxy. IsDefineKeyedOwnIC() is true when we are defining computed
      // fields in a Proxy. In these cases use the slow stub to invoke the
      // define trap.
      if (IsDefineNamedOwnIC() || IsDefineKeyedOwnIC()) {
        TRACE_HANDLER_STATS(isolate(), StoreIC_SlowStub);
        return MaybeObjectHandle(StoreHandler::StoreSlow(isolate()));
      }

      return MaybeObjectHandle(StoreHandler::StoreProxy(
          isolate(), lookup_start_object_map(), holder, receiver));
    }

    case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
    case LookupIterator::ACCESS_CHECK:
    case LookupIterator::NOT_FOUND:
    case LookupIterator::WASM_OBJECT:
      UNREACHABLE();
  }
  return MaybeObjectHandle();
}

void KeyedStoreIC::UpdateStoreElement(Handle<Map> receiver_map,
                                      KeyedAccessStoreMode store_mode,
                                      Handle<Map> new_receiver_map) {
  std::vector<MapAndHandler> target_maps_and_handlers;
  nexus()->ExtractMapsAndHandlers(
      &target_maps_and_handlers,
      [this](Handle<Map> map) { return Map::TryUpdate(isolate(), map); });
  if (target_maps_and_handlers.empty()) {
    DirectHandle<Map> monomorphic_map = receiver_map;
    // If we transitioned to a map that is a more general map than incoming
    // then use the new map.
    if (IsTransitionOfMonomorphicTarget(*receiver_map, *new_receiver_map)) {
      monomorphic_map = new_receiver_map;
    }
    Handle<Object> handler = StoreElementHandler(monomorphic_map, store_mode);
    return ConfigureVectorState(Handle<Name>(), monomorphic_map, handler);
  }

  for (const MapAndHandler& map_and_handler : target_maps_and_handlers) {
    Handle<Map> map = map_and_handler.first;
    if (!map.is_null() && map->instance_type() == JS_PRIMITIVE_WRAPPER_TYPE) {
      DCHECK(!IsStoreInArrayLiteralIC());
      set_slow_stub_reason("JSPrimitiveWrapper");
      return;
    }
  }

  // There are several special cases where an IC that is MONOMORPHIC can still
  // transition to a different IC that handles a superset of the original IC.
  // Handle those here if the receiver map hasn't changed or it has transitioned
  // to a more general kind.
  KeyedAccessStoreMode old_store_mode = GetKeyedAccessStoreMode();
  Handle<Map> previous_receiver_map = target_maps_and_handlers.at(0).first;
  if (state() == MONOMORPHIC) {
    DirectHandle<Map> transitioned_receiver_map = new_receiver_map;
    if (IsTransitionOfMonomorphicTarget(*previous_receiver_map,
                                        *transitioned_receiver_map)) {
      // If the "old" and "new" maps are in the same elements map family, or
      // if they at least come from the same origin for a transitioning store,
      // stay MONOMORPHIC and use the map for the most generic ElementsKind.
      Handle<Object> handler =
          StoreElementHandler(transitioned_receiver_map, store_mode);
      ConfigureVectorState(Handle<Name>(), transitioned_receiver_map, handler);
      return;
    }
    // If there is no transition and if we have seen the same map earlier and
    // there is only a change in the store_mode we can still stay monomorphic.
    if (receiver_map.is_identical_to(previous_receiver_map) &&
        new_receiver_map.is_identical_to(receiver_map) &&
        StoreModeIsInBounds(old_store_mode) &&
        !StoreModeIsInBounds(store_mode)) {
      if (IsJSArrayMap(*receiver_map) &&
          JSArray::MayHaveReadOnlyLength(*receiver_map)) {
        set_slow_stub_reason(
            "can't generalize store mode (potentially read-only length)");
        return;
      }
      // A "normal" IC that handles stores can switch to a version that can
      // grow at the end of the array, handle OOB accesses or copy COW arrays
      // and still stay MONOMORPHIC.
      Handle<Object> handler = StoreElementHandler(receiver_map, store_mode);
      return ConfigureVectorState(Handle<Name>(), receiver_map, handler);
    }
  }

  DCHECK(state() != GENERIC);

  bool map_added =
      AddOneReceiverMapIfMissing(&target_maps_and_handlers, receiver_map);

  if (IsTransitionOfMonomorphicTarget(*receiver_map, *new_receiver_map)) {
    map_added |=
        AddOneReceiverMapIfMissing(&target_maps_and_handlers, new_receiver_map);
  }

  if (!map_added) {
    // If the miss wasn't due to an unseen map, a polymorphic stub
    // won't help, use the megamorphic stub which can handle everything.
    set_slow_stub_reason("same map added twice");
    return;
  }

  // If the maximum number of receiver maps has been exceeded, use the
  // megamorphic version of the IC.
  if (static_cast<int>(target_maps_and_handlers.size()) >
      v8_flags.max_valid_polymorphic_map_count) {
    return;
  }

  // Make sure all polymorphic handlers have the same store mode, otherwise the
  // megamorphic stub must be used.
  if (!StoreModeIsInBounds(old_store_mode)) {
    if (StoreModeIsInBounds(store_mode)) {
      store_mode = old_store_mode;
    } else if (store_mode != old_store_mode) {
      set_slow_stub_reason("store mode mismatch");
      return;
    }
  }

  // If the store mode isn't the standard mode, make sure that all polymorphic
  // receivers are either external arrays, or all "normal" arrays with writable
  // length. Otherwise, use the megamorphic stub.
  if (!StoreModeIsInBounds(store_mode)) {
    size_t external_arrays = 0;
    for (MapAndHandler map_and_handler : target_maps_and_handlers) {
      DirectHandle<Map> map = map_and_handler.first;
      if (IsJSArrayMap(*map) && JSArray::MayHaveReadOnlyLength(*map)) {
        set_slow_stub_reason(
            "unsupported combination of arrays (potentially read-only length)");
        return;

      } else if (map->has_typed_array_or_rab_gsab_typed_array_elements()) {
        DCHECK(!IsStoreInArrayLiteralIC());
        external_arrays++;
      }
    }
    if (external_arrays != 0 &&
        external_arrays != target_maps_and_handlers.size()) {
      DCHECK(!IsStoreInArrayLiteralIC());
      set_slow_stub_reason(
          "unsupported combination of external and normal arrays");
      return;
    }
  }

  StoreElementPolymorphicHandlers(&target_maps_and_handlers, store_mode);
  if (target_maps_and_handlers.empty()) {
    Handle<Object> handler = StoreElementHandler(receiver_map, store_mode);
    ConfigureVectorState(Handle<Name>(), receiver_map, handler);
  } else if (target_maps_and_handlers.size() == 1) {
    ConfigureVectorState(Handle<Name>(), target_maps_and_handlers[0].first,
                         target_maps_and_handlers[0].second);
  } else {
    ConfigureVectorState(Handle<Name>(), target_maps_and_handlers);
  }
}

Handle<Object> KeyedStoreIC::StoreElementHandler(
    DirectHandle<Map> receiver_map, KeyedAccessStoreMode store_mode,
    MaybeHandle<UnionOf<Smi, Cell>> prev_validity_cell) {
  // The only case when could keep using non-slow element store handler for
  // a fast array with potentially read-only elements is when it's an
  // initializing store to array literal.
  DCHECK_IMPLIES(
      !receiver_map->has_dictionary_elements() &&
          receiver_map->ShouldCheckForReadOnlyElementsInPrototypeChain(
              isolate()),
      IsStoreInArrayLiteralIC());

  if (!IsJSObjectMap(*receiver_map)) {
    // DefineKeyedOwnIC, which is used to define computed fields in instances,
    // should handled by the slow stub below instead of the proxy stub.
    if (IsJSProxyMap(*receiver_map) && !IsDefineKeyedOwnIC()) {
      return StoreHandler::StoreProxy(isolate());
    }

    // Wasm objects or other kind of special objects go through the slow stub.
    TRACE_HANDLER_STATS(isolate(), KeyedStoreIC_SlowStub);
    return StoreHandler::StoreSlow(isolate(), store_mode);
  }

  // TODO(ishell): move to StoreHandler::StoreElement().
  Handle<Code> code;
  if (receiver_map->has_sloppy_arguments_elements()) {
    // TODO(jgruber): Update counter name.
    TRACE_HANDLER_STATS(isolate(), KeyedStoreIC_KeyedStoreSloppyArgumentsStub);
    code = StoreHandler::StoreSloppyArgumentsBuiltin(isolate(), store_mode);
  } else if (receiver_map->has_fast_elements() ||
             receiver_map->has_sealed_elements() ||
             receiver_map->has_nonextensible_elements() ||
             receiver_map->has_typed_array_or_rab_gsab_typed_array_elements()) {
    // TODO(jgruber): Update counter name.
    TRACE_HANDLER_STATS(isolate(), KeyedStoreIC_StoreFastElementStub);
    if (IsJSArgumentsObjectMap(*receiver_map) &&
        receiver_map->has_fast_packed_elements()) {
      // Allow fast behaviour for in-bounds stores while making it miss and
      // properly handle the out of bounds store case.
      code = StoreHandler::StoreFastElementBuiltin(
          isolate(), KeyedAccessStoreMode::kInBounds);
    } else {
      code = StoreHandler::StoreFastElementBuiltin(isolate(), store_mode);
      if (receiver_map->has_typed_array_or_rab_gsab_typed_array_elements()) {
        return code;
      }
    }
  } else if (IsStoreInArrayLiteralIC()) {
    // TODO(jgruber): Update counter name.
    TRACE_HANDLER_STATS(isolate(), StoreInArrayLiteralIC_SlowStub);
    return StoreHandler::StoreSlow(isolate(), store_mode);
  } else {
    // TODO(jgruber): Update counter name.
    TRACE_HANDLER_STATS(isolate(), KeyedStoreIC_StoreElementStub);
    DCHECK(DICTIONARY_ELEMENTS == receiver_map->elements_kind() ||
           receiver_map->has_frozen_elements());
    return StoreHandler::StoreSlow(isolate(), store_mode);
  }

  if (IsAnyDefineOwn() || IsStoreInArrayLiteralIC()) return code;
  Handle<UnionOf<Smi, Cell>> validity_cell;
  if (!prev_validity_cell.ToHandle(&validity_cell)) {
    validity_cell =
        Map::GetOrCreatePrototypeChainValidityCell(receiver_map, isolate());
  }
  if (IsSmi(*validity_cell)) {
    // There's no prototype validity cell to check, so we can just use the stub.
    return code;
  }
  Handle<StoreHandler> handler = isolate()->factory()->NewStoreHandler(0);
  handler->set_validity_cell(*validity_cell);
  handler->set_smi_handler(*code);
  return handler;
}

void KeyedStoreIC::StoreElementPolymorphicHandlers(
    std::vector<MapAndHandler>* receiver_maps_and_handlers,
    KeyedAccessStoreMode store_mode) {
  std::vector<Handle<Map>> receiver_maps;
  for (size_t i = 0; i < receiver_maps_and_handlers->size(); i++) {
    receiver_maps.push_back(receiver_maps_and_handlers->at(i).first);
  }

  for (size_t i = 0; i < receiver_maps_and_handlers->size(); i++) {
    Handle<Map> receiver_map = receiver_maps_and_handlers->at(i).first;
    DCHECK(!receiver_map->is_deprecated());
    MaybeObjectHandle old_handler = receiver_maps_and_handlers->at(i).second;
    Handle<Object> handler;
    Handle<Map> transition;

    if (receiver_map->instance_type() < FIRST_JS_RECEIVER_TYPE ||
        receiver_map->ShouldCheckForReadOnlyElementsInPrototypeChain(
            isolate())) {
      // TODO(mvstanton): Consider embedding store_mode in the state of the slow
      // keyed store ic for uniformity.
      TRACE_HANDLER_STATS(isolate(), KeyedStoreIC_SlowStub);
      handler = StoreHandler::StoreSlow(isolate());

    } else {
      {
        Tagged<Map> tmap = receiver_map->FindElementsKindTransitionedMap(
            isolate(),
            MapHandlesSpan(receiver_maps.begin(), receiver_maps.end()),
            ConcurrencyMode::kSynchronous);
        if (!tmap.is_null()) {
          if (receiver_map->is_stable()) {
            receiver_map->NotifyLeafMapLayoutChange(isolate());
          }
          transition = handle(tmap, isolate());
        }
      }

      MaybeHandle<UnionOf<Smi, Cell>> validity_cell;
      Tagged<HeapObject> old_handler_obj;
      if (!old_handler.is_null() &&
          (*old_handler).GetHeapObject(&old_handler_obj) &&
          IsDataHandler(old_handler_obj)) {
        validity_cell = handle(
            Cast<DataHandler>(old_handler_obj)->validity_cell(), isolate());
      }
      // TODO(mythria): Do not recompute the handler if we know there is no
      // change in the handler.
      // TODO(mvstanton): The code below is doing pessimistic elements
      // transitions. I would like to stop doing that and rely on Allocation
      // Site Tracking to do a better job of ensuring the data types are what
      // they need to be. Not all the elements are in place yet, pessimistic
      // elements transitions are still important for performance.
      if (!transition.is_null()) {
        TRACE_HANDLER_STATS(isolate(),
                            KeyedStoreIC_ElementsTransitionAndStoreStub);
        handler = StoreHandler::StoreElementTransition(
            isolate(), receiver_map, transition, store_mode, validity_cell);
      } else {
        handler = StoreElementHandler(receiver_map, store_mode, validity_cell);
      }
    }
    DCHECK(!handler.is_null());
    receiver_maps_and_handlers->at(i) =
        MapAndHandler(receiver_map, MaybeObjectHandle(handler));
  }
}

namespace {

bool MayHaveTypedArrayInPrototypeChain(DirectHandle<JSObject> object) {
  for (PrototypeIterator iter(object->GetIsolate(), *object); !iter.IsAtEnd();
       iter.Advance()) {
    // Be conservative, don't walk into proxies.
    if (IsJSProxy(iter.GetCurrent())) return true;
    if (IsJSTypedArray(iter.GetCurrent())) return true;
  }
  return false;
}

KeyedAccessStoreMode GetStoreMode(DirectHandle<JSObject> receiver,
                                  size_t index) {
  bool oob_access = IsOutOfBoundsAccess(receiver, index);
  // Don't consider this a growing store if the store would send the receiver to
  // dictionary mode.
  bool allow_growth =
      IsJSArray(*receiver) && oob_access && index <= JSArray::kMaxArrayIndex &&
      !receiver->WouldConvertToSlowElements(static_cast<uint32_t>(index));
  if (allow_growth) {
    return KeyedAccessStoreMode::kGrowAndHandleCOW;
  }
  if (receiver->map()->has_typed_array_or_rab_gsab_typed_array_elements() &&
      oob_access) {
    return KeyedAccessStoreMode::kIgnoreTypedArrayOOB;
  }
  return receiver->elements()->IsCowArray() ? KeyedAccessStoreMode::kHandleCOW
                                            : KeyedAccessStoreMode::kInBounds;
}

}  // namespace

MaybeHandle<Object> KeyedStoreIC::Store(Handle<Object> object,
                                        Handle<Object> key,
                                        Handle<Object> value) {
  // TODO(verwaest): Let SetProperty do the migration, since storing a property
  // might deprecate the current map again, if value does not fit.
  if (MigrateDeprecated(isolate(), object)) {
    Handle<Object> result;
    // TODO(v8:12548): refactor DefineKeyedOwnIC as a subclass of StoreIC
    // so the logic doesn't get mixed here.
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate(), result,
        IsDefineKeyedOwnIC()
            ? Runtime::DefineObjectOwnProperty(isolate(), object, key, value,
                                               StoreOrigin::kMaybeKeyed)
            : Runtime::SetObjectProperty(isolate(), object, key, value,
                                         StoreOrigin::kMaybeKeyed));
    return result;
  }

  Handle<Object> store_handle;

  intptr_t maybe_index;
  Handle<Name> maybe_name;
  KeyType key_type = TryConvertKey(key, isolate(), &maybe_index, &maybe_name);

  if (key_type == kName) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate(), store_handle,
        StoreIC::Store(object, maybe_name, value, StoreOrigin::kMaybeKeyed));
    if (vector_needs_update()) {
      if (ConfigureVectorState(MEGAMORPHIC, key)) {
        set_slow_stub_reason("unhandled internalized string key");
        TraceIC("StoreIC", key);
      }
    }
    return store_handle;
  }

  JSObject::MakePrototypesFast(object, kStartAtPrototype, isolate());

  // TODO(jkummerow): Refactor the condition logic here and below.
  bool use_ic = (state() != NO_FEEDBACK) && v8_flags.use_ic &&
                !IsStringWrapper(*object) && !IsAccessCheckNeeded(*object) &&
                !IsJSGlobalProxy(*object);
  if (use_ic && !IsSmi(*object)) {
    // Don't use ICs for maps of the objects in Array's prototype chain. We
    // expect to be able to trap element sets to objects with those maps in
    // the runtime to enable optimization of element hole access.
    DirectHandle<HeapObject> heap_object = Cast<HeapObject>(object);
    if (heap_object->map()->IsMapInArrayPrototypeChain(isolate())) {
      set_slow_stub_reason("map in array prototype");
      use_ic = false;
    }
#if V8_ENABLE_WEBASSEMBLY
    if (IsWasmObjectMap(heap_object->map())) {
      set_slow_stub_reason("wasm object");
      use_ic = false;
    }
#endif
  }

  Handle<Map> old_receiver_map;
  bool is_arguments = false;
  bool key_is_valid_index = (key_type == kIntPtr);
  KeyedAccessStoreMode store_mode = KeyedAccessStoreMode::kInBounds;
  if (use_ic && IsJSReceiver(*object) && key_is_valid_index) {
    DirectHandle<JSReceiver> receiver = Cast<JSReceiver>(object);
    old_receiver_map = handle(receiver->map(), isolate());
    is_arguments = IsJSArgumentsObject(*receiver);
    bool is_jsobject = IsJSObject(*receiver);
    size_t index;
    key_is_valid_index = IntPtrKeyToSize(maybe_index, receiver, &index);
    if (is_jsobject && !is_arguments && key_is_valid_index) {
      DirectHandle<JSObject> receiver_object = Cast<JSObject>(object);
      store_mode = GetStoreMode(receiver_object, index);
    }
  }

  DCHECK(store_handle.is_null());
  // TODO(v8:12548): refactor DefineKeyedOwnIC as a subclass of StoreIC
  // so the logic doesn't get mixed here.
  MaybeHandle<Object> result =
      IsDefineKeyedOwnIC()
          ? Runtime::DefineObjectOwnProperty(isolate(), object, key, value,
                                             StoreOrigin::kMaybeKeyed)
          : Runtime::SetObjectProperty(isolate(), object, key, value,
                                       StoreOrigin::kMaybeKeyed);
  if (result.is_null()) {
    DCHECK(isolate()->has_exception());
    set_slow_stub_reason("failed to set property");
    use_ic = false;
  }
  if (use_ic) {
    if (!old_receiver_map.is_null()) {
      if (is_arguments) {
        set_slow_stub_reason("arguments receiver");
      } else if (IsJSArray(*object) && StoreModeCanGrow(store_mode) &&
                 JSArray::HasReadOnlyLength(Cast<JSArray>(object))) {
        set_slow_stub_reason("array has read only length");
      } else if (IsJSObject(*object) &&
                 MayHaveTypedArrayInPrototypeChain(Cast<JSObject>(object))) {
        // Make sure we don't handle this in IC if there's any JSTypedArray in
        // the {receiver}'s prototype chain, since that prototype is going to
        // swallow all stores that are out-of-bounds for said prototype, and we
        // just let the runtime deal with the complexity of this.
        set_slow_stub_reason("typed array in the prototype chain");
      } else if (key_is_valid_index) {
        if (old_receiver_map->is_abandoned_prototype_map()) {
          set_slow_stub_reason("receiver with prototype map");
        } else if (old_receiver_map->has_dictionary_elements() ||
                   !old_receiver_map
                        ->ShouldCheckForReadOnlyElementsInPrototypeChain(
                            isolate())) {
          // We should go generic if receiver isn't a dictionary, but our
          // prototype chain does have dictionary elements. This ensures that
          // other non-dictionary receivers in the polymorphic case benefit
          // from fast path keyed stores.
          DirectHandle<HeapObject> receiver = Cast<HeapObject>(object);
          UpdateStoreElement(old_receiver_map, store_mode,
                             handle(receiver->map(), isolate()));
        } else {
          set_slow_stub_reason("prototype with potentially read-only elements");
        }
      } else {
        set_slow_stub_reason("non-smi-like key");
      }
    } else {
      set_slow_stub_reason("non-JSObject receiver");
    }
  }

  if (vector_needs_update()) {
    ConfigureVectorState(MEGAMORPHIC, key);
  }
  TraceIC("StoreIC", key);

  return result;
}

namespace {
Maybe<bool> StoreOwnElement(Isolate* isolate, Handle<JSArray> array,
                            Handle<Object> index, Handle<Object> value) {
  DCHECK(IsNumber(*index));
  PropertyKey key(isolate, index);
  LookupIterator it(isolate, array, key, LookupIterator::OWN);

  MAYBE_RETURN(JSObject::DefineOwnPropertyIgnoreAttributes(
                   &it, value, NONE, Just(ShouldThrow::kThrowOnError)),
               Nothing<bool>());
  return Just(true);
}
}  // namespace

MaybeHandle<Object> StoreInArrayLiteralIC::Store(Handle<JSArray> array,
                                                 Handle<Object> index,
                                                 Handle<Object> value) {
  DCHECK(!array->map()->IsMapInArrayPrototypeChain(isolate()));
  DCHECK(IsNumber(*index));

  if (!v8_flags.use_ic || state() == NO_FEEDBACK ||
      MigrateDeprecated(isolate(), array)) {
    MAYBE_RETURN_NULL(StoreOwnElement(isolate(), array, index, value));
    TraceIC("StoreInArrayLiteralIC", index);
    return value;
  }

  // TODO(neis): Convert HeapNumber to Smi if possible?

  KeyedAccessStoreMode store_mode = KeyedAccessStoreMode::kInBounds;
  if (IsSmi(*index)) {
    DCHECK_GE(Smi::ToInt(*index), 0);
    uint32_t index32 = static_cast<uint32_t>(Smi::ToInt(*index));
    store_mode = GetStoreMode(array, index32);
  }

  Handle<Map> old_array_map(array->map(), isolate());
  MAYBE_RETURN_NULL(StoreOwnElement(isolate(), array, index, value));

  if (IsSmi(*index)) {
    DCHECK(!old_array_map->is_abandoned_prototype_map());
    UpdateStoreElement(old_array_map, store_mode,
                       handle(array->map(), isolate()));
  } else {
    set_slow_stub_reason("index out of Smi range");
  }

  if (vector_needs_update()) {
    ConfigureVectorState(MEGAMORPHIC, index);
  }
  TraceIC("StoreInArrayLiteralIC", index);
  return value;
}

// ----------------------------------------------------------------------------
// Static IC stub generators.
//
//
RUNTIME_FUNCTION(Runtime_LoadIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> receiver = args.at(0);
  Handle<Name> key = args.at<Name>(1);
  int slot = args.tagged_index_value_at(2);
  Handle<FeedbackVector> vector = args.at<FeedbackVector>(3);
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);

  // A monomorphic or polymorphic KeyedLoadIC with a string key can call the
  // LoadIC miss handler if the handler misses. Since the vector Nexus is
  // set up outside the IC, handle that here.
  FeedbackSlotKind kind = vector->GetKind(vector_slot);
  if (IsLoadICKind(kind)) {
    LoadIC ic(isolate, vector, vector_slot, kind);
    ic.UpdateState(receiver, key);
    RETURN_RESULT_OR_FAILURE(isolate, ic.Load(receiver, key));

  } else if (IsLoadGlobalICKind(kind)) {
    DCHECK_EQ(isolate->native_context()->global_proxy(), *receiver);
    receiver = isolate->global_object();
    LoadGlobalIC ic(isolate, vector, vector_slot, kind);
    ic.UpdateState(receiver, key);
    RETURN_RESULT_OR_FAILURE(isolate, ic.Load(key));

  } else {
    DCHECK(IsKeyedLoadICKind(kind));
    KeyedLoadIC ic(isolate, vector, vector_slot, kind);
    ic.UpdateState(receiver, key);
    RETURN_RESULT_OR_FAILURE(isolate, ic.Load(receiver, key));
  }
}

RUNTIME_FUNCTION(Runtime_LoadNoFeedbackIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> receiver = args.at(0);
  Handle<Name> key = args.at<Name>(1);
  int slot_kind = args.smi_value_at(2);
  FeedbackSlotKind kind = static_cast<FeedbackSlotKind>(slot_kind);

  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  FeedbackSlot vector_slot = FeedbackSlot::Invalid();
  // This function is only called after looking up in the ScriptContextTable so
  // it is safe to call LoadIC::Load for global loads as well.
  LoadIC ic(isolate, vector, vector_slot, kind);
  ic.UpdateState(receiver, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Load(receiver, key));
}

RUNTIME_FUNCTION(Runtime_LoadWithReceiverNoFeedbackIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> receiver = args.at(0);
  Handle<Object> object = args.at(1);
  Handle<Name> key = args.at<Name>(2);

  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  FeedbackSlot vector_slot = FeedbackSlot::Invalid();
  LoadIC ic(isolate, vector, vector_slot, FeedbackSlotKind::kLoadProperty);
  ic.UpdateState(object, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Load(object, key, true, receiver));
}

RUNTIME_FUNCTION(Runtime_LoadGlobalIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  // Runtime functions don't follow the IC's calling convention.
  DirectHandle<JSGlobalObject> global = isolate->global_object();
  Handle<String> name = args.at<String>(0);
  int slot = args.tagged_index_value_at(1);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(2);
  int typeof_value = args.smi_value_at(3);
  TypeofMode typeof_mode = static_cast<TypeofMode>(typeof_value);
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);

  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    vector = Cast<FeedbackVector>(maybe_vector);
  }

  FeedbackSlotKind kind = (typeof_mode == TypeofMode::kInside)
                              ? FeedbackSlotKind::kLoadGlobalInsideTypeof
                              : FeedbackSlotKind::kLoadGlobalNotInsideTypeof;
  LoadGlobalIC ic(isolate, vector, vector_slot, kind);
  ic.UpdateState(global, name);

  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result, ic.Load(name));
  return *result;
}

RUNTIME_FUNCTION(Runtime_LoadGlobalIC_Slow) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<String> name = args.at<String>(0);

  int slot = args.tagged_index_value_at(1);
  Handle<FeedbackVector> vector = args.at<FeedbackVector>(2);
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
  FeedbackSlotKind kind = vector->GetKind(vector_slot);

  LoadGlobalIC ic(isolate, vector, vector_slot, kind);
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result, ic.Load(name, false));
  return *result;
}

RUNTIME_FUNCTION(Runtime_LoadWithReceiverIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> receiver = args.at(0);
  Handle<Object> object = args.at(1);
  Handle<Name> key = args.at<Name>(2);
  int slot = args.tagged_index_value_at(3);
  Handle<FeedbackVector> vector = args.at<FeedbackVector>(4);
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);

  DCHECK(IsLoadICKind(vector->GetKind(vector_slot)));
  LoadIC ic(isolate, vector, vector_slot, FeedbackSlotKind::kLoadProperty);
  ic.UpdateState(object, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Load(object, key, true, receiver));
}

RUNTIME_FUNCTION(Runtime_KeyedLoadIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> receiver = args.at(0);
  Handle<Object> key = args.at(1);
  int slot = args.tagged_index_value_at(2);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(3);

  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    vector = Cast<FeedbackVector>(maybe_vector);
  }
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
  KeyedLoadIC ic(isolate, vector, vector_slot, FeedbackSlotKind::kLoadKeyed);
  ic.UpdateState(receiver, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Load(receiver, key));
}

RUNTIME_FUNCTION(Runtime_StoreIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  int slot = args.tagged_index_value_at(1);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(2);
  Handle<Object> receiver = args.at(3);
  Handle<Name> key = args.at<Name>(4);

  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);

  // When there is no feedback vector it is OK to use the SetNamedStrict as
  // the feedback slot kind. We only reuse this for DefineNamedOwnIC when
  // installing the handler for storing const properties. This will happen only
  // when feedback vector is available.
  FeedbackSlotKind kind = FeedbackSlotKind::kSetNamedStrict;
  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    DCHECK(!vector_slot.IsInvalid());
    vector = Cast<FeedbackVector>(maybe_vector);
    kind = vector->GetKind(vector_slot);
  }

  DCHECK(IsSetNamedICKind(kind) || IsDefineNamedOwnICKind(kind));
  StoreIC ic(isolate, vector, vector_slot, kind);
  ic.UpdateState(receiver, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Store(receiver, key, value));
}

RUNTIME_FUNCTION(Runtime_DefineNamedOwnIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  int slot = args.tagged_index_value_at(1);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(2);
  Handle<Object> receiver = args.at(3);
  Handle<Name> key = args.at<Name>(4);

  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);

  // When there is no feedback vector it is OK to use the DefineNamedOwn
  // feedback kind. There _should_ be a vector, though.
  FeedbackSlotKind kind = FeedbackSlotKind::kDefineNamedOwn;
  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    DCHECK(!vector_slot.IsInvalid());
    vector = Cast<FeedbackVector>(maybe_vector);
    kind = vector->GetKind(vector_slot);
  }

  DCHECK(IsDefineNamedOwnICKind(kind));

  // TODO(v8:12548): refactor DefineNamedOwnIC as a subclass of StoreIC, which
  // can be called here.
  StoreIC ic(isolate, vector, vector_slot, kind);
  ic.UpdateState(receiver, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Store(receiver, key, value));
}

RUNTIME_FUNCTION(Runtime_DefineNamedOwnIC_Slow) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());

  Handle<Object> value = args.at(0);
  Handle<Object> object = args.at(1);
  Handle<Object> key = args.at(2);

  // Unlike DefineKeyedOwnIC, DefineNamedOwnIC doesn't handle private
  // fields and is used for defining data properties in object literals
  // and defining named public class fields.
  DCHECK(!IsSymbol(*key) || !Cast<Symbol>(*key)->is_private_name());

  PropertyKey lookup_key(isolate, key);
  MAYBE_RETURN(JSReceiver::CreateDataProperty(isolate, object, lookup_key,
                                              value, Nothing<ShouldThrow>()),
               ReadOnlyRoots(isolate).exception());
  return *value;
}

RUNTIME_FUNCTION(Runtime_StoreGlobalIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  int slot = args.tagged_index_value_at(1);
  Handle<FeedbackVector> vector = args.at<FeedbackVector>(2);
  Handle<Name> key = args.at<Name>(3);

  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
  FeedbackSlotKind kind = vector->GetKind(vector_slot);
  StoreGlobalIC ic(isolate, vector, vector_slot, kind);
  DirectHandle<JSGlobalObject> global = isolate->global_object();
  ic.UpdateState(global, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Store(key, value));
}

RUNTIME_FUNCTION(Runtime_StoreGlobalICNoFeedback_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  Handle<Name> key = args.at<Name>(1);

  // TODO(mythria): Replace StoreGlobalStrict/Sloppy with SetNamedProperty.
  StoreGlobalIC ic(isolate, Handle<FeedbackVector>(), FeedbackSlot(),
                   FeedbackSlotKind::kStoreGlobalStrict);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Store(key, value));
}

// TODO(mythria): Remove Feedback vector and slot. Since they are not used apart
// from the DCHECK.
RUNTIME_FUNCTION(Runtime_StoreGlobalIC_Slow) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  Handle<String> name = args.at<String>(4);

#ifdef DEBUG
  {
    int slot = args.tagged_index_value_at(1);
    DirectHandle<FeedbackVector> vector = args.at<FeedbackVector>(2);
    FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
    FeedbackSlotKind slot_kind = vector->GetKind(vector_slot);
    DCHECK(IsStoreGlobalICKind(slot_kind));
    DirectHandle<Object> receiver = args.at(3);
    DCHECK(IsJSGlobalProxy(*receiver));
  }
#endif

  Handle<JSGlobalObject> global = isolate->global_object();
  DirectHandle<Context> native_context = isolate->native_context();
  DirectHandle<ScriptContextTable> script_contexts(
      native_context->script_context_table(), isolate);

  VariableLookupResult lookup_result;
  if (script_contexts->Lookup(name, &lookup_result)) {
    DirectHandle<Context> script_context(
        script_contexts->get(lookup_result.context_index), isolate);
    if (IsImmutableLexicalVariableMode(lookup_result.mode)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kConstAssign, global, name));
    }

    {
      DisallowGarbageCollection no_gc;
      Tagged<Object> previous_value =
          script_context->get(lookup_result.slot_index);

      if (IsTheHole(previous_value, isolate)) {
        AllowGarbageCollection yes_gc;
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate,
            NewReferenceError(MessageTemplate::kAccessedUninitializedVariable,
                              name));
      }
    }
    if (v8_flags.const_tracking_let) {
      Context::UpdateConstTrackingLetSideData(
          script_context, lookup_result.slot_index, value, isolate);
    }
    script_context->set(lookup_result.slot_index, *value);
    return *value;
  }

  RETURN_RESULT_OR_FAILURE(
      isolate, Runtime::SetObjectProperty(isolate, global, name, value,
                                          StoreOrigin::kMaybeKeyed));
}

RUNTIME_FUNCTION(Runtime_KeyedStoreIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(2);
  Handle<Object> receiver = args.at(3);
  Handle<Object> key = args.at(4);
  FeedbackSlot vector_slot;

  // When the feedback vector is not valid the slot can only be of type
  // StoreKeyed. Storing in array literals falls back to
  // StoreInArrayLiterIC_Miss. This function is also used from store handlers
  // installed in feedback vectors. In such cases, we need to get the kind from
  // feedback vector slot since the handlers are used for both for StoreKeyed
  // and StoreInArrayLiteral kinds.
  FeedbackSlotKind kind = FeedbackSlotKind::kSetKeyedStrict;
  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    vector = Cast<FeedbackVector>(maybe_vector);
    int slot = args.tagged_index_value_at(1);
    vector_slot = FeedbackVector::ToSlot(slot);
    kind = vector->GetKind(vector_slot);
  }

  // The elements store stubs miss into this function, but they are shared by
  // different ICs.
  // TODO(v8:12548): refactor DefineKeyedOwnIC as a subclass of KeyedStoreIC,
  // which can be called here.
  if (IsKeyedStoreICKind(kind) || IsDefineKeyedOwnICKind(kind)) {
    KeyedStoreIC ic(isolate, vector, vector_slot, kind);
    ic.UpdateState(receiver, key);
    RETURN_RESULT_OR_FAILURE(isolate, ic.Store(receiver, key, value));
  } else {
    DCHECK(IsStoreInArrayLiteralICKind(kind));
    DCHECK(IsJSArray(*receiver));
    DCHECK(IsNumber(*key));
    StoreInArrayLiteralIC ic(isolate, vector, vector_slot);
    ic.UpdateState(receiver, key);
    RETURN_RESULT_OR_FAILURE(isolate,
                             ic.Store(Cast<JSArray>(receiver), key, value));
  }
}

RUNTIME_FUNCTION(Runtime_DefineKeyedOwnIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  int slot = args.tagged_index_value_at(1);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(2);
  Handle<Object> receiver = args.at(3);
  Handle<Object> key = args.at(4);
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);

  FeedbackSlotKind kind = FeedbackSlotKind::kDefineKeyedOwn;
  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    vector = Cast<FeedbackVector>(maybe_vector);
    kind = vector->GetKind(vector_slot);
    DCHECK(IsDefineKeyedOwnICKind(kind));
  }

  // TODO(v8:12548): refactor DefineKeyedOwnIC as a subclass of KeyedStoreIC,
  // which can be called here.
  KeyedStoreIC ic(isolate, vector, vector_slot, kind);
  ic.UpdateState(receiver, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Store(receiver, key, value));
}

RUNTIME_FUNCTION(Runtime_StoreInArrayLiteralIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  int slot = args.tagged_index_value_at(1);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(2);
  Handle<Object> receiver = args.at(3);
  Handle<Object> key = args.at(4);
  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    vector = Cast<FeedbackVector>(maybe_vector);
  }
  DCHECK(IsJSArray(*receiver));
  DCHECK(IsNumber(*key));
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
  StoreInArrayLiteralIC ic(isolate, vector, vector_slot);
  RETURN_RESULT_OR_FAILURE(isolate,
                           ic.Store(Cast<JSArray>(receiver), key, value));
}

RUNTIME_FUNCTION(Runtime_KeyedStoreIC_Slow) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  Handle<Object> object = args.at(1);
  Handle<Object> key = args.at(2);
  RETURN_RESULT_OR_FAILURE(
      isolate, Runtime::SetObjectProperty(isolate, object, key, value,
                                          StoreOrigin::kMaybeKeyed));
}

RUNTIME_FUNCTION(Runtime_DefineKeyedOwnIC_Slow) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  Handle<Object> object = args.at(1);
  Handle<Object> key = args.at(2);
  RETURN_RESULT_OR_FAILURE(
      isolate, Runtime::DefineObjectOwnProperty(isolate, object, key, value,
                                                StoreOrigin::kMaybeKeyed));
}

RUNTIME_FUNCTION(Runtime_StoreInArrayLiteralIC_Slow) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  Handle<Object> array = args.at(1);
  Handle<Object> index = args.at(2);
  StoreOwnElement(isolate, Cast<JSArray>(array), index, value);
  return *value;
}

RUNTIME_FUNCTION(Runtime_ElementsTransitionAndStoreIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> object = args.at(0);
  Handle<Object> key = args.at(1);
  Handle<Object> value = args.at(2);
  DirectHandle<Map> map = args.at<Map>(3);
  int slot = args.tagged_index_value_at(4);
  DirectHandle<FeedbackVector> vector = args.at<FeedbackVector>(5);
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
  FeedbackSlotKind kind = vector->GetKind(vector_slot);

  if (IsJSObject(*object)) {
    JSObject::TransitionElementsKind(Cast<JSObject>(object),
                                     map->elements_kind());
  }

  if (IsStoreInArrayLiteralICKind(kind)) {
    StoreOwnElement(isolate, Cast<JSArray>(object), key, value);
    return *value;
  } else {
    DCHECK(IsKeyedStoreICKind(kind) || IsSetNamedICKind(kind) ||
           IsDefineKeyedOwnICKind(kind));
    RETURN_RESULT_OR_FAILURE(
        isolate,
        IsDefineKeyedOwnICKind(kind)
            ? Runtime::DefineObjectOwnProperty(isolate, object, key, value,
                                               StoreOrigin::kMaybeKeyed)
            : Runtime::SetObjectProperty(isolate, object, key, value,
                                         StoreOrigin::kMaybeKeyed));
  }
}

namespace {

enum class FastCloneObjectMode {
  // The clone has the same map as the input.
  kIdenticalMap,
  // The clone is the empty object literal.
  kEmptyObject,
  // The clone has an empty object literal map.
  kDifferentMap,
  // The source map is to complicated to handle.
  kNotSupported,
};

FastCloneObjectMode GetCloneModeForMap(DirectHandle<Map> map,
                                       bool null_proto_literal,
                                       Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  if (!IsJSObjectMap(*map)) {
    // Everything that produces the empty object literal can be supported since
    // we have a special case for that.
    if (null_proto_literal) return FastCloneObjectMode::kNotSupported;
    return IsNullOrUndefinedMap(*map) || IsBooleanMap(*map) ||
                   IsHeapNumberMap(*map)
               ? FastCloneObjectMode::kEmptyObject
               : FastCloneObjectMode::kNotSupported;
  }
  ElementsKind elements_kind = map->elements_kind();
  if (!IsSmiOrObjectElementsKind(elements_kind) &&
      !IsAnyNonextensibleElementsKind(elements_kind)) {
    return FastCloneObjectMode::kNotSupported;
  }
  if (!map->OnlyHasSimpleProperties()) {
    return FastCloneObjectMode::kNotSupported;
  }

  // TODO(olivf): Think about cases where cross-context copies are safe.
  if (*map->map()->native_context() != *isolate->native_context()) {
    return FastCloneObjectMode::kNotSupported;
  }

  // The clone must always start from an object literal map, it must be an
  // instance of the object function, have the default prototype and not be a
  // prototype itself. Only if the source map fits that criterion we can
  // directly use it as the target map.
  FastCloneObjectMode mode =
      map->instance_type() == JS_OBJECT_TYPE &&
              !IsAnyNonextensibleElementsKind(elements_kind) &&
              map->GetConstructor() == *isolate->object_function() &&
              map->prototype() == *isolate->object_function_prototype() &&
              !map->is_prototype_map()
          ? FastCloneObjectMode::kIdenticalMap
          : FastCloneObjectMode::kDifferentMap;

  if (null_proto_literal || IsNull(map->prototype())) {
    mode = FastCloneObjectMode::kDifferentMap;
  }

  Tagged<DescriptorArray> descriptors = map->instance_descriptors();
  for (InternalIndex i : map->IterateOwnDescriptors()) {
    PropertyDetails details = descriptors->GetDetails(i);
    Tagged<Name> key = descriptors->GetKey(i);
    if (details.kind() != PropertyKind::kData || !details.IsEnumerable() ||
        key->IsPrivateName()) {
      return FastCloneObjectMode::kNotSupported;
    }
    if (!details.IsConfigurable() || details.IsReadOnly()) {
      mode = FastCloneObjectMode::kDifferentMap;
    }
  }

  DCHECK_IMPLIES(mode == FastCloneObjectMode::kIdenticalMap,
                 !map->is_prototype_map());

  return mode;
}

bool CanCacheCloneTargetMapTransition(
    DirectHandle<Map> source_map, std::optional<DirectHandle<Map>> target_map,
    bool null_proto_literal, Isolate* isolate) {
  if (!v8_flags.clone_object_sidestep_transitions || null_proto_literal) {
    return false;
  }
  // As of now any R/O source object should end up in the kEmptyObject case, but
  // there is not really a way of ensuring it. Thus, we also check it below.
  // This is a performance dcheck. If it fails, the clone IC does not handle a
  // case it probably could.
  // TODO(olivf): Either remove that dcheck or move it to GetCloneModeForMap.
  DCHECK(!InReadOnlySpace(*source_map));
  if (InReadOnlySpace(*source_map) || source_map->is_deprecated() ||
      source_map->is_prototype_map() ||
      !TransitionsAccessor::CanHaveMoreTransitions(isolate, source_map)) {
    return false;
  }
  if (!target_map) {
    return true;
  }
  CHECK(!InReadOnlySpace(**target_map));
  return !(*target_map)->is_deprecated();
}

// Check if an object with `source_map` can be cloned by `FastCloneJSObject`
// when the result shall have `target_map`. Optionally `override_map` is the map
// of an already existing object that will be written into. If no `override_map`
// is given, we assume that a fresh target object can be allocated with
// already the correct `target_map`.
bool CanFastCloneObjectToObjectLiteral(DirectHandle<Map> source_map,
                                       DirectHandle<Map> target_map,
                                       DirectHandle<Map> override_map,
                                       bool null_proto_literal,
                                       Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  DCHECK(!target_map->is_deprecated());
  DCHECK(source_map->OnlyHasSimpleProperties());
  DCHECK(!source_map->IsInobjectSlackTrackingInProgress());
  DCHECK(!target_map->IsInobjectSlackTrackingInProgress());
  DCHECK_EQ(*target_map->map(), *source_map->map());
  DCHECK_EQ(target_map->GetConstructor(), *isolate->object_function());
  DCHECK_IMPLIES(
      !null_proto_literal,
      *target_map->prototype() == *isolate->object_function_prototype());

  // Ensure source and target have identical binary represenation of properties
  // and elements as the IC relies on copying the raw bytes. This also excludes
  // cases with non-enumerable properties or accessors on the source object.
  if (source_map->instance_type() != JS_OBJECT_TYPE ||
      target_map->instance_type() != JS_OBJECT_TYPE ||
      !target_map->OnlyHasSimpleProperties() ||
      !target_map->has_fast_elements()) {
    return false;
  }
  if (!override_map.is_null()) {
    // No cross-context object reuse.
    if (target_map->map() != override_map->map()) {
      return false;
    }
    // In case we want to clone into an existing target object, we must ensure
    // that this existing object has a compatible size. In particular we cannot
    // shrink or grow the already given object. We also exclude a different
    // start offset, since this doesn't allow us to change the object in-place
    // in a GC safe way.
    DCHECK_EQ(*override_map, isolate->object_function()->initial_map());
    DCHECK(override_map->instance_type() == JS_OBJECT_TYPE);
    DCHECK_EQ(override_map->NumberOfOwnDescriptors(), 0);
    DCHECK(!override_map->IsInobjectSlackTrackingInProgress());
    if (override_map->instance_size() != target_map->instance_size() ||
        override_map->GetInObjectPropertiesStartInWords() !=
            target_map->GetInObjectPropertiesStartInWords()) {
      return false;
    }
  }
#ifdef DEBUG
  ElementsKind source_elements_kind = source_map->elements_kind();
  ElementsKind target_elements_kind = target_map->elements_kind();
  DCHECK(IsSmiOrObjectElementsKind(source_elements_kind) ||
         IsAnyNonextensibleElementsKind(source_elements_kind));
  DCHECK(IsSmiOrObjectElementsKind(target_elements_kind));
  DCHECK_IMPLIES(IsHoleyElementsKindForRead(source_elements_kind),
                 IsHoleyElementsKind(target_elements_kind));
#endif  // DEBUG
  // There are no transitions between prototype maps.
  if (source_map->is_prototype_map() || target_map->is_prototype_map()) {
    return false;
  }
  // Exclude edge-cases like not copying a __proto__ property.
  if (source_map->NumberOfOwnDescriptors() !=
      target_map->NumberOfOwnDescriptors()) {
    return false;
  }
  // Check that the source inobject properties fit into the target.
  int source_used_inobj_properties = source_map->GetInObjectProperties() -
                                     source_map->UnusedInObjectProperties();
  int target_used_inobj_properties = target_map->GetInObjectProperties() -
                                     target_map->UnusedInObjectProperties();
  if (source_used_inobj_properties != target_used_inobj_properties) {
    return false;
  }
  // The properties backing store must be of the same size as the clone ic again
  // blindly copies it.
  if (source_map->HasOutOfObjectProperties() !=
          target_map->HasOutOfObjectProperties() ||
      (target_map->HasOutOfObjectProperties() &&
       source_map->UnusedPropertyFields() !=
           target_map->UnusedPropertyFields())) {
    return false;
  }
  Tagged<DescriptorArray> descriptors = source_map->instance_descriptors();
  Tagged<DescriptorArray> target_descriptors =
      target_map->instance_descriptors();
  for (InternalIndex i : target_map->IterateOwnDescriptors()) {
    if (descriptors->GetKey(i) != target_descriptors->GetKey(i)) {
      return false;
    }
    PropertyDetails details = descriptors->GetDetails(i);
    PropertyDetails target_details = target_descriptors->GetDetails(i);
    DCHECK_EQ(details.kind(), PropertyKind::kData);
    DCHECK_EQ(target_details.kind(), PropertyKind::kData);
    Tagged<FieldType> type = descriptors->GetFieldType(i);
    Tagged<FieldType> target_type = target_descriptors->GetFieldType(i);
    // This DCHECK rests on the fact that we only clear field types when there
    // are no instances of the host map left. Thus, to enter the clone IC at
    // least one object of the source map needs to be created, which in turn
    // will re-initialize the source maps field type. This is guaranteed to also
    // update the target map through the sidestep transition, unless the target
    // map is deprecated.
    DCHECK(!IsNone(type));
    DCHECK(!IsNone(target_type));
    // With move_prototype_transitions_first enabled field updates don't
    // generalize across prototype transitions, because the transitions happen
    // on root maps (i.e., before any field is added). In other words we cannot
    // rely on changes in the source map propagating to the target map when
    // there is a SetPrototype involved. NB, technically without
    // move_prototype_transitions_first we also don't update field types across
    // prototype transitions, however we preemptively generalize all fields of
    // prototype transition target maps.
    bool prototype_transition_is_shortcutted =
        v8_flags.move_prototype_transitions_first &&
        source_map->prototype() != target_map->prototype();
    if (!prototype_transition_is_shortcutted &&
        CanCacheCloneTargetMapTransition(source_map, target_map,
                                         null_proto_literal, isolate)) {
      if (!details.representation().fits_into(
              target_details.representation()) ||
          (target_details.representation().IsDouble() &&
           details.representation().IsSmi())) {
        return false;
      }
      if (!FieldType::NowIs(type, target_type)) {
        return false;
      }
    } else {
      // In the case we cannot connect the maps in the transition tree (e.g.,
      // the clone also involves a proto transition) we cannot keep track of
      // representation dependencies. We can only allow the most generic target
      // representation. The same goes for field types.
      if (!details.representation().MostGenericInPlaceChange().Equals(
              target_details.representation()) ||
          !IsAny(target_type)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

static MaybeHandle<JSObject> CloneObjectSlowPath(Isolate* isolate,
                                                 Handle<Object> source,
                                                 int flags) {
  Handle<JSObject> new_object;
  if (flags & ObjectLiteral::kHasNullPrototype) {
    new_object = isolate->factory()->NewJSObjectWithNullProto();
  } else if (IsJSObject(*source) &&
             Cast<JSObject>(*source)->map()->OnlyHasSimpleProperties()) {
    Tagged<Map> source_map = Cast<JSObject>(*source)->map();
    // TODO(olivf, chrome:1204540) It might be interesting to pick a map with
    // more properties, depending how many properties are added by the
    // surrounding literal.
    int properties = source_map->GetInObjectProperties() -
                     source_map->UnusedInObjectProperties();
    DirectHandle<Map> map = isolate->factory()->ObjectLiteralMapFromCache(
        isolate->native_context(), properties);
    new_object = isolate->factory()->NewFastOrSlowJSObjectFromMap(map);
  } else {
    Handle<JSFunction> constructor(isolate->native_context()->object_function(),
                                   isolate);
    new_object = isolate->factory()->NewJSObject(constructor);
  }

  if (IsNullOrUndefined(*source)) {
    return new_object;
  }

  MAYBE_RETURN(
      JSReceiver::SetOrCopyDataProperties(
          isolate, new_object, source,
          PropertiesEnumerationMode::kPropertyAdditionOrder, nullptr, false),
      MaybeHandle<JSObject>());
  return new_object;
}

RUNTIME_FUNCTION(Runtime_CloneObjectIC_Slow) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> source = args.at(0);
  int flags = args.smi_value_at(1);
  RETURN_RESULT_OR_FAILURE(isolate,
                           CloneObjectSlowPath(isolate, source, flags));
}

namespace {

template <SideStepTransition::Kind kind>
Tagged<Object> GetCloneTargetMap(Isolate* isolate, DirectHandle<Map> source_map,
                                 DirectHandle<Map> override_map) {
  static_assert(kind == SideStepTransition::Kind::kObjectAssign ||
                kind == SideStepTransition::Kind::kCloneObject);
  if (!v8_flags.clone_object_sidestep_transitions) return {};
  Tagged<Object> result = SideStepTransition::Empty;
  TransitionsAccessor transitions(isolate, *source_map);
  if (transitions.HasSideStepTransitions()) {
    result = transitions.GetSideStepTransition(kind);
    if (result.IsHeapObject()) {
      // Exclude deprecated maps.
      bool is_valid = !Cast<Map>(result.GetHeapObject())->is_deprecated();
      // In the case of object assign we need to check the prototype validity
      // cell on the override map. If the override map changed we cannot assume
      // that it is correct to set all properties without any getter/setter in
      // the prototype chain interfering.
      if constexpr (kind == SideStepTransition::Kind::kObjectAssign) {
        if (is_valid) {
          DCHECK_EQ(*override_map, isolate->object_function()->initial_map());
          Tagged<Object> validity_cell = transitions.GetSideStepTransition(
              SideStepTransition::Kind::kObjectAssignValidityCell);
          is_valid = validity_cell.IsHeapObject() &&
                     Cast<Cell>(validity_cell)->value().ToSmi().value() ==
                         Map::kPrototypeChainValid;
        }
      }
      if (V8_UNLIKELY(!is_valid)) {
        result = SideStepTransition::Empty;
      }
    }
  }
#ifdef DEBUG
  FastCloneObjectMode clone_mode =
      GetCloneModeForMap(source_map, false, isolate);
  if (result == SideStepTransition::Unreachable) {
    switch (clone_mode) {
      case FastCloneObjectMode::kNotSupported:
      case FastCloneObjectMode::kDifferentMap:
        break;
      case FastCloneObjectMode::kEmptyObject:
      case FastCloneObjectMode::kIdenticalMap:
        DCHECK_EQ(kind, SideStepTransition::Kind::kObjectAssign);
        break;
    }
  } else if (result != SideStepTransition::Empty) {
    Tagged<Map> target = Cast<Map>(result.GetHeapObject());
    switch (clone_mode) {
      case FastCloneObjectMode::kIdenticalMap:
        if (kind == SideStepTransition::Kind::kCloneObject) {
          DCHECK_EQ(*source_map, target);
          break;
        }
        DCHECK_EQ(kind, SideStepTransition::Kind::kObjectAssign);
        [[fallthrough]];
      case FastCloneObjectMode::kDifferentMap:
        DCHECK(CanFastCloneObjectToObjectLiteral(
            source_map, handle(target, isolate), override_map, false, isolate));
        break;
      default:
        UNREACHABLE();
    }
  } else {
    DCHECK_EQ(result, SideStepTransition::Empty);
  }
#endif  // DEBUG
  return result;
}

template <SideStepTransition::Kind kind>
void SetCloneTargetMap(Isolate* isolate, Handle<Map> source_map,
                       DirectHandle<Map> new_target_map,
                       DirectHandle<Map> override_map) {
  if (!v8_flags.clone_object_sidestep_transitions) return;
  DCHECK(CanCacheCloneTargetMapTransition(source_map, new_target_map, false,
                                          isolate));
  DCHECK_EQ(GetCloneTargetMap<kind>(isolate, source_map, override_map),
            SideStepTransition::Empty);
  DCHECK(!new_target_map->is_deprecated());

  // Adding this transition also ensures that when the source map field
  // generalizes, we also generalize the target map.
  DCHECK(IsSmiOrObjectElementsKind(new_target_map->elements_kind()));

  constexpr bool need_validity_cell =
      kind == SideStepTransition::Kind::kObjectAssign;
  DirectHandle<Cell> validity_cell;
  if constexpr (need_validity_cell) {
    // Since we only clone into empty object literals we only need one validity
    // cell on that prototype chain.
    DCHECK_EQ(*override_map, isolate->object_function()->initial_map());
    validity_cell = Cast<Cell>(
        Map::GetOrCreatePrototypeChainValidityCell(override_map, isolate));
  }
  TransitionsAccessor::EnsureHasSideStepTransitions(isolate, source_map);
  TransitionsAccessor transitions(isolate, *source_map);
  transitions.SetSideStepTransition(kind, *new_target_map);
  if constexpr (need_validity_cell) {
    transitions.SetSideStepTransition(
        SideStepTransition::Kind::kObjectAssignValidityCell, *validity_cell);
  }
  DCHECK_EQ(GetCloneTargetMap<kind>(isolate, source_map, override_map),
            *new_target_map);
}

template <SideStepTransition::Kind kind>
void SetCloneTargetMapUnsupported(Isolate* isolate, Handle<Map> source_map,
                                  DirectHandle<Map> override_map) {
  if (!v8_flags.clone_object_sidestep_transitions) return;
  DCHECK_EQ(GetCloneTargetMap<kind>(isolate, source_map, override_map),
            SideStepTransition::Empty);
  DCHECK(CanCacheCloneTargetMapTransition(source_map, {}, false, isolate));
  // Adding this transition also ensures that when the source map field
  // generalizes, we also generalize the target map.
  TransitionsAccessor::EnsureHasSideStepTransitions(isolate, source_map);
  TransitionsAccessor(isolate, *source_map)
      .SetSideStepTransition(kind, SideStepTransition::Unreachable);
  DCHECK_EQ(GetCloneTargetMap<kind>(isolate, source_map, override_map),
            SideStepTransition::Unreachable);
}

}  // namespace

RUNTIME_FUNCTION(Runtime_CloneObjectIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<Object> source = args.at(0);
  int flags = args.smi_value_at(1);

  if (!MigrateDeprecated(isolate, source)) {
    Handle<HeapObject> maybe_vector = args.at<HeapObject>(3);
    std::optional<FeedbackNexus> nexus;
    if (IsFeedbackVector(*maybe_vector)) {
      int index = args.tagged_index_value_at(2);
      FeedbackSlot slot = FeedbackVector::ToSlot(index);
      nexus.emplace(isolate, Cast<FeedbackVector>(maybe_vector), slot);
    }
    if (!IsSmi(*source) && (!nexus || !nexus->IsMegamorphic())) {
      bool null_proto_literal = flags & ObjectLiteral::kHasNullPrototype;
      Handle<Map> source_map(Cast<HeapObject>(source)->map(), isolate);

      // In case we are still slack tracking let's defer a decision. The fast
      // case does not support it.
      if (!source_map->IsInobjectSlackTrackingInProgress()) {
        auto UpdateNexus = [&](Handle<Object> target_map) {
          if (!nexus) return;
          nexus->ConfigureCloneObject(source_map,
                                      MaybeObjectHandle(target_map));
        };
        ReadOnlyRoots roots(isolate);
        bool unsupported = false;
        if (!null_proto_literal) {
          auto maybe_target =
              GetCloneTargetMap<SideStepTransition::Kind::kCloneObject>(
                  isolate, source_map, {});
          if (maybe_target == SideStepTransition::Unreachable) {
            unsupported = true;
          } else if (maybe_target != SideStepTransition::Empty) {
            Handle<Map> target =
                handle(Cast<Map>(maybe_target.GetHeapObject()), isolate);
            UpdateNexus(target);
            return *target;
          }
        }

        FastCloneObjectMode clone_mode =
            unsupported
                ? FastCloneObjectMode::kNotSupported
                : GetCloneModeForMap(source_map, null_proto_literal, isolate);
        auto UpdateState = [&](Handle<Map> target_map) {
          UpdateNexus(target_map);
          if (CanCacheCloneTargetMapTransition(source_map, target_map,
                                               null_proto_literal, isolate)) {
            SetCloneTargetMap<SideStepTransition::Kind::kCloneObject>(
                isolate, source_map, target_map, {});
          }
        };
        switch (clone_mode) {
          case FastCloneObjectMode::kIdenticalMap: {
            UpdateState(source_map);
            // When returning a map the IC miss handler re-starts from the top.
            return *source_map;
          }
          case FastCloneObjectMode::kEmptyObject: {
            UpdateNexus(handle(Smi::zero(), isolate));
            RETURN_RESULT_OR_FAILURE(
                isolate, CloneObjectSlowPath(isolate, source, flags));
          }
          case FastCloneObjectMode::kDifferentMap: {
            Handle<Object> res;
            ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
                isolate, res, CloneObjectSlowPath(isolate, source, flags));
            Handle<Map> result_map(Cast<HeapObject>(res)->map(), isolate);
            if (result_map->IsInobjectSlackTrackingInProgress()) {
              return *res;
            }
            if (CanFastCloneObjectToObjectLiteral(
                    source_map, result_map, {}, null_proto_literal, isolate)) {
              DCHECK(result_map->OnlyHasSimpleProperties());
              DCHECK_EQ(source_map->GetInObjectProperties() -
                            source_map->UnusedInObjectProperties(),
                        result_map->GetInObjectProperties() -
                            result_map->UnusedInObjectProperties());
              UpdateState(result_map);
            } else {
              if (CanCacheCloneTargetMapTransition(
                      source_map, {}, null_proto_literal, isolate)) {
                SetCloneTargetMapUnsupported<
                    SideStepTransition::Kind::kCloneObject>(isolate, source_map,
                                                            {});
              }
              if (nexus) {
                nexus->ConfigureMegamorphic();
              }
            }
            return *res;
          }
          case FastCloneObjectMode::kNotSupported: {
            break;
          }
        }
        DCHECK(clone_mode == FastCloneObjectMode::kNotSupported);
        if (nexus) {
          nexus->ConfigureMegamorphic();
        }
      }
    }
  }

  RETURN_RESULT_OR_FAILURE(isolate,
                           CloneObjectSlowPath(isolate, source, flags));
}

RUNTIME_FUNCTION(Runtime_StoreCallbackProperty) {
  Handle<JSObject> receiver = args.at<JSObject>(0);
  DirectHandle<JSObject> holder = args.at<JSObject>(1);
  DirectHandle<AccessorInfo> info = args.at<AccessorInfo>(2);
  Handle<Name> name = args.at<Name>(3);
  Handle<Object> value = args.at(4);
  HandleScope scope(isolate);

#ifdef V8_RUNTIME_CALL_STATS
  if (V8_UNLIKELY(TracingFlags::is_runtime_stats_enabled())) {
    RETURN_RESULT_OR_FAILURE(
        isolate, Runtime::SetObjectProperty(isolate, receiver, name, value,
                                            StoreOrigin::kMaybeKeyed));
  }
#endif

  PropertyCallbackArguments arguments(isolate, info->data(), *receiver, *holder,
                                      Nothing<ShouldThrow>());
  std::ignore = arguments.CallAccessorSetter(info, name, value);
  RETURN_FAILURE_IF_EXCEPTION(isolate);
  return *value;
}

namespace {

bool MaybeCanCloneObjectForObjectAssign(Handle<JSReceiver> source,
                                        DirectHandle<Map> source_map,
                                        Handle<JSReceiver> target,
                                        Isolate* isolate) {
  FastCloneObjectMode clone_mode =
      GetCloneModeForMap(source_map, false, isolate);
  switch (clone_mode) {
    case FastCloneObjectMode::kIdenticalMap:
    case FastCloneObjectMode::kDifferentMap:
      break;
    case FastCloneObjectMode::kNotSupported:
      return false;
    case FastCloneObjectMode::kEmptyObject:
      // Cannot happen since we should only be called with JSObjects.
      UNREACHABLE();
  }

  // We need to be sure that there are no setters or other nastiness installed
  // on the Object.prototype which clash with the properties we intende to copy.
  Handle<FixedArray> keys;
  auto res =
      KeyAccumulator::GetKeys(isolate, source, KeyCollectionMode::kOwnOnly,
                              ONLY_ENUMERABLE, GetKeysConversion::kKeepNumbers);
  CHECK(res.ToHandle(&keys));
  for (int i = 0; i < keys->length(); ++i) {
    Handle<Object> next_key(keys->get(i), isolate);
    PropertyKey key(isolate, next_key);
    LookupIterator it(isolate, target, key);
    switch (it.state()) {
      case LookupIterator::NOT_FOUND:
        break;
      case LookupIterator::DATA:
        if (it.property_attributes() & PropertyAttributes::READ_ONLY) {
          return false;
        }
        break;
      default:
        return false;
    }
  }
  return true;
}

}  // namespace

// Returns one of:
// * A map to be used with FastCloneJSObject
// * Undefined if fast cloning is not possible
// * True if assignment must be skipped (i.e., the runtime already did it)
RUNTIME_FUNCTION(Runtime_ObjectAssignTryFastcase) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  auto source = Cast<JSReceiver>(args.at(0));
  auto target = Cast<JSReceiver>(args.at(1));
  DCHECK(IsJSObject(*source));
  DCHECK(IsJSObject(*target));

  Handle<Map> source_map = handle(source->map(), isolate);
  Handle<Map> target_map = handle(target->map(), isolate);

  DCHECK_EQ(target_map->NumberOfOwnDescriptors(), 0);
  DCHECK(!source_map->is_dictionary_map());
  DCHECK(!target_map->is_dictionary_map());
  DCHECK(!source_map->is_deprecated());
  DCHECK(!target_map->is_deprecated());
  DCHECK(target_map->is_extensible());
  DCHECK(!IsUndefined(*source, isolate) && !IsNull(*source, isolate));

  ReadOnlyRoots roots(isolate);
  {
    Tagged<Object> maybe_clone_target =
        GetCloneTargetMap<SideStepTransition::Kind::kObjectAssign>(
            isolate, source_map, target_map);
    if (maybe_clone_target == SideStepTransition::Unreachable) {
      return roots.undefined_value();
    } else if (maybe_clone_target != SideStepTransition::Empty) {
      return Cast<Map>(maybe_clone_target.GetHeapObject());
    }
  }

  auto UpdateCache = [&](Handle<Map> clone_target_map) {
    if (CanCacheCloneTargetMapTransition(source_map, clone_target_map, false,
                                         isolate)) {
      SetCloneTargetMap<SideStepTransition::Kind::kObjectAssign>(
          isolate, source_map, clone_target_map, target_map);
    }
  };
  auto UpdateCacheNotClonable = [&]() {
    SetCloneTargetMapUnsupported<SideStepTransition::Kind::kObjectAssign>(
        isolate, source_map, target_map);
  };

  // In case we are still slack tracking let's defer a decision. The fast case
  // does not support it.
  if (source_map->IsInobjectSlackTrackingInProgress() ||
      target_map->IsInobjectSlackTrackingInProgress()) {
    return roots.undefined_value();
  }

  if (MaybeCanCloneObjectForObjectAssign(source, source_map, target, isolate)) {
    CHECK(target->map()->OnlyHasSimpleProperties());
    Maybe<bool> res = JSReceiver::SetOrCopyDataProperties(
        isolate, target, source, PropertiesEnumerationMode::kEnumerationOrder);
    DCHECK(res.FromJust());
    USE(res);
    Handle<Map> clone_target_map = handle(target->map(), isolate);
    if (clone_target_map->IsInobjectSlackTrackingInProgress()) {
      return roots.true_value();
    }
    if (CanFastCloneObjectToObjectLiteral(source_map, clone_target_map,
                                          target_map, false, isolate)) {
      CHECK(target->map()->OnlyHasSimpleProperties());
      UpdateCache(clone_target_map);
    } else {
      UpdateCacheNotClonable();
    }
    // We already did the copying here. Thus, returning true to cause the
    // CSA builtin to skip assigning anything.
    return roots.true_value();
  }
  UpdateCacheNotClonable();
  return roots.undefined_value();
}

/**
 * Loads a property with an interceptor performing post interceptor
 * lookup if interceptor failed.
 */
RUNTIME_FUNCTION(Runtime_LoadPropertyWithInterceptor) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  Handle<Name> name = args.at<Name>(0);
  Handle<Object> receiver = args.at(1);
  Handle<JSObject> holder = args.at<JSObject>(2);

  if (!IsJSReceiver(*receiver)) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, receiver, Object::ConvertReceiver(isolate, receiver));
  }

  {
    Handle<InterceptorInfo> interceptor(holder->GetNamedInterceptor(), isolate);
    PropertyCallbackArguments arguments(isolate, interceptor->data(), *receiver,
                                        *holder, Just(kDontThrow));

    Handle<Object> result = arguments.CallNamedGetter(interceptor, name);
    // An exception was thrown in the interceptor. Propagate.
    RETURN_FAILURE_IF_EXCEPTION_DETECTOR(isolate, arguments);

    if (!result.is_null()) {
      arguments.AcceptSideEffects();
      return *result;
    }
    // If the interceptor didn't handle the request, then there must be no
    // side effects.
  }

  LookupIterator it(isolate, receiver, name, holder);
  // Skip any lookup work until we hit the (possibly non-masking) interceptor.
  while (it.state() != LookupIterator::INTERCEPTOR ||
         !it.GetHolder<JSObject>().is_identical_to(holder)) {
    DCHECK(it.state() != LookupIterator::ACCESS_CHECK || it.HasAccess());
    it.Next();
  }
  // Skip past the interceptor.
  it.Next();
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result, Object::GetProperty(&it));

  if (it.IsFound()) return *result;

  int slot = args.tagged_index_value_at(3);
  DirectHandle<FeedbackVector> vector = args.at<FeedbackVector>(4);
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
  FeedbackSlotKind slot_kind = vector->GetKind(vector_slot);
  // It could actually be any kind of load IC slot here but the predicate
  // handles all the cases properly.
  if (!LoadIC::ShouldThrowReferenceError(slot_kind)) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  // Throw a reference error.
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewReferenceError(MessageTemplate::kNotDefined, it.name()));
}

RUNTIME_FUNCTION(Runtime_StorePropertyWithInterceptor) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> value = args.at(0);
  Handle<JSObject> receiver = args.at<JSObject>(1);
  Handle<Name> name = args.at<Name>(2);

  // TODO(ishell): Cache interceptor_holder in the store handler like we do
  // for LoadHandler::kInterceptor case.
  DirectHandle<JSObject> interceptor_holder = receiver;
  if (IsJSGlobalProxy(*receiver) &&
      (!receiver->HasNamedInterceptor() ||
       receiver->GetNamedInterceptor()->non_masking())) {
    interceptor_holder =
        handle(Cast<JSObject>(receiver->map()->prototype()), isolate);
  }
  DCHECK(interceptor_holder->HasNamedInterceptor());
  {
    DirectHandle<InterceptorInfo> interceptor(
        interceptor_holder->GetNamedInterceptor(), isolate);

    DCHECK(!interceptor->non_masking());
    // TODO(ishell, 348688196): why is it known that it shouldn't throw?
    Maybe<ShouldThrow> should_throw = Just(kDontThrow);
    PropertyCallbackArguments args(isolate, interceptor->data(), *receiver,
                                   *receiver, should_throw);

    v8::Intercepted intercepted =
        args.CallNamedSetter(interceptor, name, value);
    // Stores initiated by StoreICs don't care about the exact result of
    // the store operation returned by the callback as long as it doesn't
    // throw an exception.
    constexpr bool ignore_return_value = true;
    InterceptorResult result;
    MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result,
        args.GetBooleanReturnValue(intercepted, "Setter", ignore_return_value));

    switch (result) {
      case InterceptorResult::kFalse:
      case InterceptorResult::kTrue:
        return *value;

      case InterceptorResult::kNotIntercepted:
        // Proceed storing past the interceptor.
        break;
    }
  }

  LookupIterator it(isolate, receiver, name, receiver);
  // Skip past any access check on the receiver.
  while (it.state() == LookupIterator::ACCESS_CHECK) {
    DCHECK(it.HasAccess());
    it.Next();
  }
  // Skip past the interceptor on the receiver.
  DCHECK_EQ(LookupIterator::INTERCEPTOR, it.state());
  it.Next();

  MAYBE_RETURN(Object::SetProperty(&it, value, StoreOrigin::kNamed),
               ReadOnlyRoots(isolate).exception());
  return *value;
}

RUNTIME_FUNCTION(Runtime_LoadElementWithInterceptor) {
  // TODO(verwaest): This should probably get the holder and receiver as input.
  HandleScope scope(isolate);
  Handle<JSObject> receiver = args.at<JSObject>(0);
  DCHECK_GE(args.smi_value_at(1), 0);
  uint32_t index = args.smi_value_at(1);

  Handle<InterceptorInfo> interceptor(receiver->GetIndexedInterceptor(),
                                      isolate);
  PropertyCallbackArguments arguments(isolate, interceptor->data(), *receiver,
                                      *receiver, Just(kDontThrow));
  Handle<Object> result = arguments.CallIndexedGetter(interceptor, index);
  // An exception was thrown in the interceptor. Propagate.
  RETURN_FAILURE_IF_EXCEPTION_DETECTOR(isolate, arguments);

  if (result.is_null()) {
    LookupIterator it(isolate, receiver, index, receiver);
    DCHECK_EQ(LookupIterator::INTERCEPTOR, it.state());
    it.Next();
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                       Object::GetProperty(&it));
  }

  return *result;
}

RUNTIME_FUNCTION(Runtime_KeyedHasIC_Miss) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  // Runtime functions don't follow the IC's calling convention.
  Handle<Object> receiver = args.at(0);
  Handle<Object> key = args.at(1);
  int slot = args.tagged_index_value_at(2);
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(3);

  Handle<FeedbackVector> vector = Handle<FeedbackVector>();
  if (!IsUndefined(*maybe_vector, isolate)) {
    DCHECK(IsFeedbackVector(*maybe_vector));
    vector = Cast<FeedbackVector>(maybe_vector);
  }
  FeedbackSlot vector_slot = FeedbackVector::ToSlot(slot);
  KeyedLoadIC ic(isolate, vector, vector_slot, FeedbackSlotKind::kHasKeyed);
  ic.UpdateState(receiver, key);
  RETURN_RESULT_OR_FAILURE(isolate, ic.Load(receiver, key));
}

RUNTIME_FUNCTION(Runtime_HasElementWithInterceptor) {
  HandleScope scope(isolate);
  Handle<JSObject> receiver = args.at<JSObject>(0);
  DCHECK_GE(args.smi_value_at(1), 0);
  uint32_t index = args.smi_value_at(1);

  {
    Handle<InterceptorInfo> interceptor(receiver->GetIndexedInterceptor(),
                                        isolate);
    PropertyCallbackArguments arguments(isolate, interceptor->data(), *receiver,
                                        *receiver, Just(kDontThrow));

    if (!IsUndefined(interceptor->query(), isolate)) {
      Handle<Object> result = arguments.CallIndexedQuery(interceptor, index);
      // An exception was thrown in the interceptor. Propagate.
      RETURN_FAILURE_IF_EXCEPTION_DETECTOR(isolate, arguments);
      if (!result.is_null()) {
        int32_t value;
        CHECK(Object::ToInt32(*result, &value));
        // TODO(ishell): PropertyAttributes::ABSENT is not exposed in the Api,
        // so it can't be officially returned. We should fix the tests instead.
        if (value == ABSENT) return ReadOnlyRoots(isolate).false_value();
        arguments.AcceptSideEffects();
        return ReadOnlyRoots(isolate).true_value();
      }
    } else if (!IsUndefined(interceptor->getter(), isolate)) {
      Handle<Object> result = arguments.CallIndexedGetter(interceptor, index);
      // An exception was thrown in the interceptor. Propagate.
      RETURN_FAILURE_IF_EXCEPTION_DETECTOR(isolate, arguments);
      if (!result.is_null()) {
        arguments.AcceptSideEffects();
        return ReadOnlyRoots(isolate).true_value();
      }
    }
    // If the interceptor didn't handle the request, then there must be no
    // side effects.
  }

  LookupIterator it(isolate, receiver, index, receiver);
  DCHECK_EQ(LookupIterator::INTERCEPTOR, it.state());
  it.Next();
  Maybe<bool> maybe = JSReceiver::HasProperty(&it);
  if (maybe.IsNothing()) return ReadOnlyRoots(isolate).exception();
  return ReadOnlyRoots(isolate).boolean_value(maybe.FromJust());
}

}  // namespace internal
}  // namespace v8

// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/heap-refs.h"

#include <optional>

#include "src/compiler/js-heap-broker.h"
#include "src/objects/elements-kind.h"

#ifdef ENABLE_SLOW_DCHECKS
#include <algorithm>
#endif

#include "src/api/api-inl.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/js-heap-broker-inl.h"
#include "src/execution/protectors-inl.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/property-cell.h"
#include "src/objects/template-objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(broker, x) TRACE_BROKER(broker, x)
#define TRACE_MISSING(broker, x) TRACE_BROKER_MISSING(broker, x)

// There are several kinds of ObjectData values.
//
// kSmi: The underlying V8 object is a Smi and the data is an instance of the
//   base class (ObjectData), i.e. it's basically just the handle.  Because the
//   object is a Smi, it's safe to access the handle in order to extract the
//   number value, and AsSmi() does exactly that.
//
// kBackgroundSerializedHeapObject: The underlying V8 object is a HeapObject
//   and the data is an instance of the corresponding (most-specific) subclass,
//   e.g. JSFunctionData, which provides serialized information about the
//   object. Allows serialization from the background thread.
//
// kUnserializedHeapObject: The underlying V8 object is a HeapObject and the
//   data is an instance of the base class (ObjectData), i.e. it basically
//   carries no information other than the handle.
//
// kNeverSerializedHeapObject: The underlying V8 object is a (potentially
//   mutable) HeapObject and the data is an instance of ObjectData. Its handle
//   must be persistent so that the GC can update it at a safepoint. Via this
//   handle, the object can be accessed concurrently to the main thread.
//
// kUnserializedReadOnlyHeapObject: The underlying V8 object is a read-only
//   HeapObject and the data is an instance of ObjectData. For
//   ReadOnlyHeapObjects, it is OK to access heap even from off-thread, so
//   these objects need not be serialized.
enum ObjectDataKind {
  kSmi,
  kBackgroundSerializedHeapObject,
  kUnserializedHeapObject,
  kNeverSerializedHeapObject,
  kUnserializedReadOnlyHeapObject
};

namespace {

bool Is64() { return kSystemPointerSize == 8; }

}  // namespace

class ObjectData : public ZoneObject {
 public:
  ObjectData(JSHeapBroker* broker, ObjectData** storage, Handle<Object> object,
             ObjectDataKind kind)
      : object_(object),
        kind_(kind)
#ifdef DEBUG
        ,
        broker_(broker)
#endif  // DEBUG
  {
    // This assignment ensures we don't end up inserting the same object
    // in an endless recursion.
    *storage = this;

    TRACE(broker, "Creating data " << this << " for handle " << object.address()
                                   << " (" << Brief(*object) << ")");

    // It is safe to access read only heap objects and builtins from a
    // background thread. When we read fields of these objects, we may create
    // ObjectData on the background thread.
    // This is safe too since we don't create handles but just get handles from
    // read only root table or builtins table.
    // All other objects need to be canonicalized in a persistent handle scope.
    // See CanonicalPersistentHandle().
    Isolate* isolate = broker->isolate();
    USE(isolate);
    DCHECK_IMPLIES(broker->mode() == JSHeapBroker::kDisabled ||
                       broker->mode() == JSHeapBroker::kSerializing,
                   PersistentHandlesScope::IsActive(isolate) &&
                       broker->IsCanonicalHandle(object));
    DCHECK_IMPLIES(broker->mode() == JSHeapBroker::kSerialized,
                   kind == kUnserializedReadOnlyHeapObject || kind == kSmi ||
                       kind == kNeverSerializedHeapObject ||
                       kind == kBackgroundSerializedHeapObject);
    DCHECK_IMPLIES(kind == kUnserializedReadOnlyHeapObject,
                   i::IsHeapObject(*object) &&
                       ReadOnlyHeap::Contains(Cast<HeapObject>(*object)));
  }

#define DECLARE_IS(Name) bool Is##Name() const;
  HEAP_BROKER_OBJECT_LIST(DECLARE_IS)
#undef DECLARE_IS

#define DECLARE_AS(Name) Name##Data* As##Name();
  HEAP_BROKER_BACKGROUND_SERIALIZED_OBJECT_LIST(DECLARE_AS)
#undef DECLARE_AS

  Handle<Object> object() const { return object_; }
  ObjectDataKind kind() const { return kind_; }
  bool is_smi() const { return kind_ == kSmi; }
  bool should_access_heap() const {
    return kind_ == kUnserializedHeapObject ||
           kind_ == kNeverSerializedHeapObject ||
           kind_ == kUnserializedReadOnlyHeapObject;
  }
  bool IsNull() const { return i::IsNull(*object_); }

#ifdef DEBUG
  JSHeapBroker* broker() const { return broker_; }
#endif  // DEBUG

 private:
  Handle<Object> const object_;
  ObjectDataKind const kind_;
#ifdef DEBUG
  JSHeapBroker* const broker_;  // For DCHECKs.
#endif                          // DEBUG
};

class HeapObjectData : public ObjectData {
 public:
  HeapObjectData(JSHeapBroker* broker, ObjectData** storage,
                 Handle<HeapObject> object, ObjectDataKind kind);

  std::optional<bool> TryGetBooleanValue(JSHeapBroker* broker) const;
  ObjectData* map() const { return map_; }
  InstanceType GetMapInstanceType() const;

 private:
  std::optional<bool> TryGetBooleanValueImpl(JSHeapBroker* broker) const;

  ObjectData* const map_;
};

class PropertyCellData : public HeapObjectData {
 public:
  PropertyCellData(JSHeapBroker* broker, ObjectData** storage,
                   Handle<PropertyCell> object, ObjectDataKind kind);

  bool Cache(JSHeapBroker* broker);

  PropertyDetails property_details() const {
    CHECK(serialized());
    return property_details_;
  }

  ObjectData* value() const {
    DCHECK(serialized());
    return value_;
  }

 private:
  PropertyDetails property_details_ = PropertyDetails::Empty();
  ObjectData* value_ = nullptr;

  bool serialized() const { return value_ != nullptr; }
};

namespace {

ZoneVector<Address> GetCFunctions(Tagged<FixedArray> function_overloads,
                                  Isolate* isolate, Zone* zone) {
  const int len = function_overloads->length() /
                  FunctionTemplateInfo::kFunctionOverloadEntrySize;
  ZoneVector<Address> c_functions = ZoneVector<Address>(len, zone);
  for (int i = 0; i < len; i++) {
    c_functions[i] = v8::ToCData<kCFunctionTag>(
        isolate, function_overloads->get(
                     FunctionTemplateInfo::kFunctionOverloadEntrySize * i));
  }
  return c_functions;
}

ZoneVector<const CFunctionInfo*> GetCSignatures(
    Tagged<FixedArray> function_overloads, Isolate* isolate, Zone* zone) {
  const int len = function_overloads->length() /
                  FunctionTemplateInfo::kFunctionOverloadEntrySize;
  ZoneVector<const CFunctionInfo*> c_signatures =
      ZoneVector<const CFunctionInfo*>(len, zone);
  for (int i = 0; i < len; i++) {
    c_signatures[i] = v8::ToCData<const CFunctionInfo*, kCFunctionInfoTag>(
        isolate, function_overloads->get(
                     FunctionTemplateInfo::kFunctionOverloadEntrySize * i + 1));
  }
  return c_signatures;
}

}  // namespace

PropertyCellData::PropertyCellData(JSHeapBroker* broker, ObjectData** storage,
                                   Handle<PropertyCell> object,
                                   ObjectDataKind kind)
    : HeapObjectData(broker, storage, object, kind) {}

bool PropertyCellData::Cache(JSHeapBroker* broker) {
  if (serialized()) return true;

  TraceScope tracer(broker, this, "PropertyCellData::Serialize");
  auto cell = Cast<PropertyCell>(object());

  // While this code runs on a background thread, the property cell might
  // undergo state transitions via calls to PropertyCell::Transition. These
  // transitions follow a certain protocol on which we rely here to ensure that
  // we only report success when we can guarantee consistent data. A key
  // property is that after transitioning from cell type A to B (A != B), there
  // will never be a transition back to A, unless A is kConstant and the new
  // value is the hole (i.e. the property cell was invalidated, which is a final
  // state).

  PropertyDetails property_details = cell->property_details(kAcquireLoad);

  Handle<Object> value =
      broker->CanonicalPersistentHandle(cell->value(kAcquireLoad));
  if (broker->ObjectMayBeUninitialized(value)) {
    DCHECK(!broker->IsMainThread());
    return false;
  }

  {
    PropertyDetails property_details_again =
        cell->property_details(kAcquireLoad);
    if (property_details != property_details_again) {
      DCHECK(!broker->IsMainThread());
      return false;
    }
  }

  if (property_details.cell_type() == PropertyCellType::kInTransition) {
    DCHECK(!broker->IsMainThread());
    return false;
  }

  ObjectData* value_data = broker->TryGetOrCreateData(value);
  if (value_data == nullptr) {
    DCHECK(!broker->IsMainThread());
    return false;
  }

  PropertyCell::CheckDataIsCompatible(property_details, *value);

  DCHECK(!serialized());
  property_details_ = property_details;
  value_ = value_data;
  DCHECK(serialized());
  return true;
}

class JSReceiverData : public HeapObjectData {
 public:
  JSReceiverData(JSHeapBroker* broker, ObjectData** storage,
                 Handle<JSReceiver> object, ObjectDataKind kind)
      : HeapObjectData(broker, storage, object, kind) {}
};

class JSObjectData : public JSReceiverData {
 public:
  JSObjectData(JSHeapBroker* broker, ObjectData** storage,
               Handle<JSObject> object, ObjectDataKind kind)
      : JSReceiverData(broker, storage, object, kind) {}
};

namespace {

// Separate function for racy HeapNumber value read, so that we can explicitly
// suppress it in TSAN (see tools/sanitizers/tsan_suppressions.txt).
// We prevent inlining of this function in TSAN builds, so that TSAN does indeed
// see that this is where the race is, and does indeed ignore it.
#ifdef V8_IS_TSAN
V8_NOINLINE
#endif
uint64_t RacyReadHeapNumberBits(Tagged<HeapNumber> value) {
  return value->value_as_bits();
}

std::optional<Tagged<Object>> GetOwnFastConstantDataPropertyFromHeap(
    JSHeapBroker* broker, JSObjectRef holder, Representation representation,
    FieldIndex field_index) {
  std::optional<Tagged<Object>> constant;
  {
    DisallowGarbageCollection no_gc;
    PtrComprCageBase cage_base = broker->cage_base();

    // This check to ensure the live map is the same as the cached map to
    // to protect us against reads outside the bounds of the heap. This could
    // happen if the Ref was created in a prior GC epoch, and the object
    // shrunk in size. It might end up at the edge of a heap boundary. If
    // we see that the map is the same in this GC epoch, we are safe.
    Tagged<Map> map = holder.object()->map(cage_base, kAcquireLoad);
    if (*holder.map(broker).object() != map) {
      TRACE_BROKER_MISSING(broker, "Map changed for " << holder);
      return {};
    }

    if (field_index.is_inobject()) {
      constant =
          holder.object()->RawInobjectPropertyAt(cage_base, map, field_index);
      if (!constant.has_value()) {
        TRACE_BROKER_MISSING(
            broker, "Constant field in " << holder << " is unsafe to read");
        return {};
      }
    } else {
      Tagged<Object> raw_properties_or_hash =
          holder.object()->raw_properties_or_hash(cage_base, kRelaxedLoad);
      // Ensure that the object is safe to inspect.
      if (broker->ObjectMayBeUninitialized(raw_properties_or_hash)) {
        return {};
      }
      if (!IsPropertyArray(raw_properties_or_hash, cage_base)) {
        TRACE_BROKER_MISSING(
            broker,
            "Expected PropertyArray for backing store in " << holder << ".");
        return {};
      }
      Tagged<PropertyArray> properties =
          Cast<PropertyArray>(raw_properties_or_hash);
      const int array_index = field_index.outobject_array_index();
      if (array_index < properties->length(kAcquireLoad)) {
        constant = properties->get(array_index);
      } else {
        TRACE_BROKER_MISSING(
            broker, "Backing store for " << holder << " not long enough.");
        return {};
      }
    }
    // We might read the uninitialized sentinel, if we race with the main
    // thread adding a new property to the object (having set the map, but not
    // yet initialised the property value). Since this is a tight race, it won't
    // happen very often, so we can just abort the load.
    // TODO(leszeks): We could instead sleep/yield and spin the load, since the
    // timing on this is tight enough that we wouldn't delay the compiler thread
    // by much.
    if (IsUninitialized(constant.value())) {
      TRACE_BROKER_MISSING(broker, "Read uninitialized property.");
      return {};
    }

    // {constant} needs to pass the gc predicate before we can introspect on it.
    if (broker->ObjectMayBeUninitialized(constant.value())) return {};

    // Ensure that {constant} matches the {representation} we expect for the
    // field.
    if (!Object::FitsRepresentation(*constant, representation, false)) {
      const char* repString = IsSmi(*constant)          ? "Smi"
                              : IsHeapNumber(*constant) ? "HeapNumber"
                                                        : "HeapObject";
      TRACE_BROKER_MISSING(broker, "Mismatched representation for "
                                       << holder << ". Expected "
                                       << representation << ", but object is a "
                                       << repString);
      return {};
    }
  }
  return constant;
}

// Tries to get the property at {dict_index}. If we are within bounds of the
// object, we are guaranteed to see valid heap words even if the data is wrong.
OptionalObjectRef GetOwnDictionaryPropertyFromHeap(
    JSHeapBroker* broker, DirectHandle<JSObject> receiver,
    InternalIndex dict_index) {
  Handle<Object> constant;
  {
    DisallowGarbageCollection no_gc;
    // DictionaryPropertyAt will check that we are within the bounds of the
    // object.
    std::optional<Tagged<Object>> maybe_constant =
        JSObject::DictionaryPropertyAt(receiver, dict_index,
                                       broker->isolate()->heap());
    DCHECK_IMPLIES(broker->IsMainThread(), maybe_constant);
    if (!maybe_constant) return {};
    constant = broker->CanonicalPersistentHandle(maybe_constant.value());
  }
  return TryMakeRef(broker, constant);
}

}  // namespace

class JSTypedArrayData : public JSObjectData {
 public:
  JSTypedArrayData(JSHeapBroker* broker, ObjectData** storage,
                   Handle<JSTypedArray> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {}
};

class JSDataViewData : public JSObjectData {
 public:
  JSDataViewData(JSHeapBroker* broker, ObjectData** storage,
                 Handle<JSDataView> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {}
};

class JSPrimitiveWrapperData : public JSObjectData {
 public:
  JSPrimitiveWrapperData(JSHeapBroker* broker, ObjectData** storage,
                         Handle<JSPrimitiveWrapper> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {}
};

class JSBoundFunctionData : public JSObjectData {
 public:
  JSBoundFunctionData(JSHeapBroker* broker, ObjectData** storage,
                      Handle<JSBoundFunction> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {}
};

class JSFunctionData : public JSObjectData {
 public:
  JSFunctionData(JSHeapBroker* broker, ObjectData** storage,
                 Handle<JSFunction> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {
    Cache(broker);
  }

  bool IsConsistentWithHeapState(JSHeapBroker* broker) const;

  bool has_initial_map() const {
    DCHECK(serialized_);
    return has_initial_map_;
  }
  bool has_instance_prototype() const {
    DCHECK(serialized_);
    return has_instance_prototype_;
  }
  bool PrototypeRequiresRuntimeLookup() const {
    DCHECK(serialized_);
    return PrototypeRequiresRuntimeLookup_;
  }

  ObjectData* context() const {
    DCHECK(serialized_);
    return context_;
  }
  ObjectData* initial_map() const {
    DCHECK(serialized_);
    return initial_map_;
  }
  ObjectData* instance_prototype() const {
    DCHECK(serialized_);
    return instance_prototype_;
  }
  ObjectData* shared() const {
    DCHECK(serialized_);
    return shared_;
  }
  ObjectData* raw_feedback_cell() const {
    DCHECK(serialized_);
    return feedback_cell_;
  }
  int initial_map_instance_size_with_min_slack() const {
    DCHECK(serialized_);
    return initial_map_instance_size_with_min_slack_;
  }

  // Track serialized fields that are actually used, in order to relax
  // ConsistentJSFunctionView dependency validation as much as possible.
  enum UsedField {
    kHasFeedbackVector = 1 << 0,
    kPrototypeOrInitialMap = 1 << 1,
    kHasInitialMap = 1 << 2,
    kHasInstancePrototype = 1 << 3,
    kPrototypeRequiresRuntimeLookup = 1 << 4,
    kInitialMap = 1 << 5,
    kInstancePrototype = 1 << 6,
    kFeedbackVector = 1 << 7,
    kFeedbackCell = 1 << 8,
    kInitialMapInstanceSizeWithMinSlack = 1 << 9,
  };

  bool has_any_used_field() const { return used_fields_ != 0; }
  bool has_used_field(UsedField used_field) const {
    return (used_fields_ & used_field) != 0;
  }
  void set_used_field(UsedField used_field) { used_fields_ |= used_field; }

 private:
  void Cache(JSHeapBroker* broker);

#ifdef DEBUG
  bool serialized_ = false;
#endif  // DEBUG

  using UsedFields = base::Flags<UsedField>;
  UsedFields used_fields_;

  ObjectData* prototype_or_initial_map_ = nullptr;
  bool has_initial_map_ = false;
  bool has_instance_prototype_ = false;
  bool PrototypeRequiresRuntimeLookup_ = false;

  ObjectData* context_ = nullptr;
  ObjectData* initial_map_ =
      nullptr;  // Derives from prototype_or_initial_map_.
  ObjectData* instance_prototype_ =
      nullptr;  // Derives from prototype_or_initial_map_.
  ObjectData* shared_ = nullptr;
  ObjectData* feedback_cell_ = nullptr;
  int initial_map_instance_size_with_min_slack_;  // Derives from
                                                  // prototype_or_initial_map_.
};

class BigIntData : public HeapObjectData {
 public:
  BigIntData(JSHeapBroker* broker, ObjectData** storage, Handle<BigInt> object,
             ObjectDataKind kind)
      : HeapObjectData(broker, storage, object, kind),
        as_uint64_(object->AsUint64(nullptr)),
        as_int64_(object->AsInt64(&lossless_)) {}

  uint64_t AsUint64() const { return as_uint64_; }
  int64_t AsInt64(bool* lossless) const {
    *lossless = lossless_;
    return as_int64_;
  }

 private:
  const uint64_t as_uint64_;
  const int64_t as_int64_;
  bool lossless_;
};

struct PropertyDescriptor {
  FieldIndex field_index;
  ObjectData* field_owner = nullptr;
};

class MapData : public HeapObjectData {
 public:
  MapData(JSHeapBroker* broker, ObjectData** storage, Handle<Map> object,
          ObjectDataKind kind);

  InstanceType instance_type() const { return instance_type_; }
  int instance_size() const { return instance_size_; }
  uint32_t bit_field3() const { return bit_field3_; }
  int in_object_properties() const {
    CHECK(InstanceTypeChecker::IsJSObject(instance_type()));
    return in_object_properties_;
  }
  int UnusedPropertyFields() const { return unused_property_fields_; }
  bool is_abandoned_prototype_map() const {
    return is_abandoned_prototype_map_;
  }

 private:
  // The following fields should be const in principle, but construction
  // requires locking the MapUpdater lock. For this reason, it's easier to
  // initialize these inside the constructor body, not in the initializer list.

  InstanceType instance_type_;
  int instance_size_;
  uint32_t bit_field3_;
  int unused_property_fields_;
  bool is_abandoned_prototype_map_;
  int in_object_properties_;
};

namespace {

int InstanceSizeWithMinSlack(JSHeapBroker* broker, MapRef map) {
  // This operation is split into two phases (1. map collection, 2. map
  // processing). This is to avoid having to take two locks
  // (full_transition_array_access and map_updater_access) at once and thus
  // having to deal with related deadlock issues.
  ZoneVector<Handle<Map>> maps(broker->zone());
  maps.push_back(map.object());

  {
    DisallowGarbageCollection no_gc;

    // Has to be an initial map.
    DCHECK(IsUndefined(map.object()->GetBackPointer(), broker->isolate()));

    static constexpr bool kConcurrentAccess = true;
    TransitionsAccessor(broker->isolate(), *map.object(), kConcurrentAccess)
        .TraverseTransitionTree([&](Tagged<Map> m) {
          maps.push_back(broker->CanonicalPersistentHandle(m));
        });
  }

  // The lock is needed for UnusedPropertyFields and InstanceSizeFromSlack.
  JSHeapBroker::MapUpdaterGuardIfNeeded mumd_scope(broker);

  int slack = std::numeric_limits<int>::max();
  for (DirectHandle<Map> m : maps) {
    slack = std::min(slack, m->UnusedPropertyFields());
  }

  return map.object()->InstanceSizeFromSlack(slack);
}

}  // namespace

// IMPORTANT: Keep this sync'd with JSFunctionData::IsConsistentWithHeapState.
void JSFunctionData::Cache(JSHeapBroker* broker) {
  DCHECK(!serialized_);

  TraceScope tracer(broker, this, "JSFunctionData::Cache");
  DirectHandle<JSFunction> function = Cast<JSFunction>(object());

  // This function may run on the background thread and thus must be individual
  // fields in a thread-safe manner. Consistency between fields is *not*
  // guaranteed here, instead we verify it in `IsConsistentWithHeapState`,
  // called during job finalization. Relaxed loads are thus okay: we're
  // guaranteed to see an initialized JSFunction object, and after
  // initialization fields remain in a valid state.

  ContextRef context =
      MakeRefAssumeMemoryFence(broker, function->context(kRelaxedLoad));
  context_ = context.data();

  SharedFunctionInfoRef shared =
      MakeRefAssumeMemoryFence(broker, function->shared(kRelaxedLoad));
  shared_ = shared.data();

  if (function->has_prototype_slot()) {
    prototype_or_initial_map_ = broker->GetOrCreateData(
        function->prototype_or_initial_map(kAcquireLoad), kAssumeMemoryFence);

    has_initial_map_ = prototype_or_initial_map_->IsMap();
    if (has_initial_map_) {
      // MapData is not used for initial_map_ because some
      // AlwaysSharedSpaceJSObject subclass constructors (e.g. SharedArray) have
      // initial maps in RO space, which can be accessed directly.
      initial_map_ = prototype_or_initial_map_;

      MapRef initial_map_ref = TryMakeRef<Map>(broker, initial_map_).value();
      if (initial_map_ref.IsInobjectSlackTrackingInProgress()) {
        initial_map_instance_size_with_min_slack_ =
            InstanceSizeWithMinSlack(broker, initial_map_ref);
      } else {
        initial_map_instance_size_with_min_slack_ =
            initial_map_ref.instance_size();
      }
      CHECK_GT(initial_map_instance_size_with_min_slack_, 0);
    }

    if (has_initial_map_) {
      has_instance_prototype_ = true;
      instance_prototype_ =
          MakeRefAssumeMemoryFence(
              broker, Cast<Map>(initial_map_->object())->prototype())
              .data();
    } else if (prototype_or_initial_map_->IsHeapObject() &&
               !IsTheHole(
                   *Cast<HeapObject>(prototype_or_initial_map_->object()))) {
      has_instance_prototype_ = true;
      instance_prototype_ = prototype_or_initial_map_;
    }
  }

  PrototypeRequiresRuntimeLookup_ = function->PrototypeRequiresRuntimeLookup();

  FeedbackCellRef feedback_cell = MakeRefAssumeMemoryFence(
      broker, function->raw_feedback_cell(kAcquireLoad));
  feedback_cell_ = feedback_cell.data();

#ifdef DEBUG
  serialized_ = true;
#endif  // DEBUG
}

// IMPORTANT: Keep this sync'd with JSFunctionData::Cache.
bool JSFunctionData::IsConsistentWithHeapState(JSHeapBroker* broker) const {
  DCHECK(serialized_);

  DirectHandle<JSFunction> f = Cast<JSFunction>(object());

  if (*context_->object() != f->context()) {
    TRACE_BROKER_MISSING(broker, "JSFunction::context");
    return false;
  }

  CHECK_EQ(*shared_->object(), f->shared());

  if (f->has_prototype_slot()) {
    if (has_used_field(kPrototypeOrInitialMap) &&
        *prototype_or_initial_map_->object() !=
            f->prototype_or_initial_map(kAcquireLoad)) {
      TRACE_BROKER_MISSING(broker, "JSFunction::prototype_or_initial_map");
      return false;
    }
    if (has_used_field(kHasInitialMap) &&
        has_initial_map_ != f->has_initial_map()) {
      TRACE_BROKER_MISSING(broker, "JSFunction::has_initial_map");
      return false;
    }
    if (has_used_field(kHasInstancePrototype) &&
        has_instance_prototype_ != f->has_instance_prototype()) {
      TRACE_BROKER_MISSING(broker, "JSFunction::has_instance_prototype");
      return false;
    }
  } else {
    DCHECK(!has_initial_map_);
    DCHECK(!has_instance_prototype_);
  }

  if (has_initial_map()) {
    if (has_used_field(kInitialMap) &&
        *initial_map_->object() != f->initial_map()) {
      TRACE_BROKER_MISSING(broker, "JSFunction::initial_map");
      return false;
    }
    if (has_used_field(kInitialMapInstanceSizeWithMinSlack) &&
        initial_map_instance_size_with_min_slack_ !=
            f->ComputeInstanceSizeWithMinSlack(f->GetIsolate())) {
      TRACE_BROKER_MISSING(broker,
                           "JSFunction::ComputeInstanceSizeWithMinSlack");
      return false;
    }
  } else {
    DCHECK_NULL(initial_map_);
  }

  if (has_instance_prototype_) {
    if (has_used_field(kInstancePrototype) &&
        *instance_prototype_->object() != f->instance_prototype()) {
      TRACE_BROKER_MISSING(broker, "JSFunction::instance_prototype");
      return false;
    }
  } else {
    DCHECK_NULL(instance_prototype_);
  }

  if (has_used_field(kPrototypeRequiresRuntimeLookup) &&
      PrototypeRequiresRuntimeLookup_ != f->PrototypeRequiresRuntimeLookup()) {
    TRACE_BROKER_MISSING(broker, "JSFunction::PrototypeRequiresRuntimeLookup");
    return false;
  }

  if (has_used_field(kFeedbackCell) &&
      *feedback_cell_->object() != f->raw_feedback_cell()) {
    TRACE_BROKER_MISSING(broker, "JSFunction::raw_feedback_cell");
    return false;
  }

  return true;
}

bool JSFunctionRef::IsConsistentWithHeapState(JSHeapBroker* broker) const {
  DCHECK(broker->IsMainThread());
  return data()->AsJSFunction()->IsConsistentWithHeapState(broker);
}

HeapObjectData::HeapObjectData(JSHeapBroker* broker, ObjectData** storage,
                               Handle<HeapObject> object, ObjectDataKind kind)
    : ObjectData(broker, storage, object, kind),
      map_(broker->GetOrCreateData(
          object->map(broker->cage_base(), kAcquireLoad), kAssumeMemoryFence)) {
  CHECK_IMPLIES(broker->mode() == JSHeapBroker::kSerialized,
                kind == kBackgroundSerializedHeapObject);
}

std::optional<bool> HeapObjectData::TryGetBooleanValue(
    JSHeapBroker* broker) const {
  // Keep in sync with Object::BooleanValue.
  auto result = TryGetBooleanValueImpl(broker);
  DCHECK_IMPLIES(
      broker->IsMainThread() && result.has_value(),
      result.value() == Object::BooleanValue(*object(), broker->isolate()));
  return result;
}

std::optional<bool> HeapObjectData::TryGetBooleanValueImpl(
    JSHeapBroker* broker) const {
  DisallowGarbageCollection no_gc;
  Tagged<Object> o = *object();
  Isolate* isolate = broker->isolate();
  const InstanceType t = GetMapInstanceType();
  if (IsTrue(o, isolate)) {
    return true;
  } else if (IsFalse(o, isolate)) {
    return false;
  } else if (IsNullOrUndefined(o, isolate)) {
    return false;
  } else if (MapRef{map()}.is_undetectable()) {
    return false;  // Undetectable object is false.
  } else if (InstanceTypeChecker::IsString(t)) {
    // TODO(jgruber): Implement in possible cases.
    return {};
  } else if (InstanceTypeChecker::IsHeapNumber(t)) {
    return {};
  } else if (InstanceTypeChecker::IsBigInt(t)) {
    return {};
  }
  return true;
}

InstanceType HeapObjectData::GetMapInstanceType() const {
  ObjectData* map_data = map();
  if (map_data->should_access_heap()) {
    // The map instance type is used to check if a static_cast to the right
    // subclass is valid. We shouldn't read the value from the heap except if
    // it's coming from the ReadOnly pages.
    SBXCHECK_EQ(map_data->kind(), kUnserializedReadOnlyHeapObject);
    return Cast<Map>(map_data->object())->instance_type();
  }
  if (this == map_data) {
    // Handle infinite recursion in case this object is a contextful meta map.
    return MAP_TYPE;
  }
  return map_data->AsMap()->instance_type();
}

namespace {

bool IsReadOnlyLengthDescriptor(Isolate* isolate,
                                DirectHandle<Map> jsarray_map) {
  DCHECK(!jsarray_map->is_dictionary_map());
  Tagged<DescriptorArray> descriptors =
      jsarray_map->instance_descriptors(isolate, kRelaxedLoad);
  static_assert(
      JSArray::kLengthOffset == JSObject::kHeaderSize,
      "The length should be the first property on the descriptor array");
  InternalIndex offset(0);
  return descriptors->GetDetails(offset).IsReadOnly();
}

// Important: this predicate does not check Protectors::IsNoElementsIntact. The
// compiler checks protectors through the compilation dependency mechanism; it
// doesn't make sense to do that here as part of every MapData construction.
// Callers *must* take care to take the correct dependency themselves.
bool SupportsFastArrayIteration(JSHeapBroker* broker, DirectHandle<Map> map) {
  return map->instance_type() == JS_ARRAY_TYPE &&
         IsFastElementsKind(map->elements_kind()) &&
         IsJSArray(map->prototype()) &&
         broker->IsArrayOrObjectPrototype(broker->CanonicalPersistentHandle(
             Cast<JSArray>(map->prototype())));
}

bool SupportsFastArrayResize(JSHeapBroker* broker, DirectHandle<Map> map) {
  return SupportsFastArrayIteration(broker, map) && map->is_extensible() &&
         !map->is_dictionary_map() &&
         !IsReadOnlyLengthDescriptor(broker->isolate(), map);
}

}  // namespace

MapData::MapData(JSHeapBroker* broker, ObjectData** storage, Handle<Map> object,
                 ObjectDataKind kind)
    : HeapObjectData(broker, storage, object, kind) {
  // This lock ensure that MapData can always be background-serialized, i.e.
  // while the lock is held the Map object may not be modified (except in
  // benign ways).
  // TODO(jgruber): Consider removing this lock by being smrt.
  JSHeapBroker::MapUpdaterGuardIfNeeded mumd_scope(broker);

  // When background serializing the map, we can perform a lite serialization
  // since the MapRef will read some of the Map's fields can be read directly.

  // Even though MapRefs can read {instance_type} directly, other classes depend
  // on {instance_type} being serialized.
  instance_type_ = object->instance_type();
  instance_size_ = object->instance_size();

  // Both bit_field3 (and below bit_field) are special fields: Even though most
  // of the individual bits inside of the bitfield could be read / written
  // non-atomically, the bitfield itself has to use atomic relaxed accessors
  // since some fields since can be modified in live objects.
  // TODO(solanes, v8:7790): Assess if adding the exclusive lock in more places
  // (e.g for set_has_non_instance_prototype) makes sense. Pros: these fields
  // can use the non-atomic accessors. Cons: We would be acquiring an exclusive
  // lock in more places.
  bit_field3_ = object->relaxed_bit_field3();
  is_abandoned_prototype_map_ = object->is_abandoned_prototype_map();
  if (IsJSObjectMap(*object)) {
    unused_property_fields_ = object->UnusedPropertyFields();
    in_object_properties_ = object->GetInObjectProperties();
  } else {
    unused_property_fields_ = 0;
    in_object_properties_ = 0;
  }
}

class FixedArrayBaseData : public HeapObjectData {
 public:
  FixedArrayBaseData(JSHeapBroker* broker, ObjectData** storage,
                     Handle<FixedArrayBase> object, ObjectDataKind kind)
      : HeapObjectData(broker, storage, object, kind),
        length_(object->length(kAcquireLoad)) {}

  int length() const { return length_; }

 private:
  int const length_;
};

class FixedArrayData : public FixedArrayBaseData {
 public:
  FixedArrayData(JSHeapBroker* broker, ObjectData** storage,
                 Handle<FixedArray> object, ObjectDataKind kind)
      : FixedArrayBaseData(broker, storage, object, kind) {}
};

// Only used in JSNativeContextSpecialization.
class ScriptContextTableData : public FixedArrayBaseData {
 public:
  ScriptContextTableData(JSHeapBroker* broker, ObjectData** storage,
                         Handle<ScriptContextTable> object, ObjectDataKind kind)
      : FixedArrayBaseData(broker, storage, object, kind) {}
};

class JSArrayData : public JSObjectData {
 public:
  JSArrayData(JSHeapBroker* broker, ObjectData** storage,
              Handle<JSArray> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {}
};

class JSGlobalObjectData : public JSObjectData {
 public:
  JSGlobalObjectData(JSHeapBroker* broker, ObjectData** storage,
                     Handle<JSGlobalObject> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {}
};

class JSGlobalProxyData : public JSObjectData {
 public:
  JSGlobalProxyData(JSHeapBroker* broker, ObjectData** storage,
                    Handle<JSGlobalProxy> object, ObjectDataKind kind)
      : JSObjectData(broker, storage, object, kind) {}
};

#define DEFINE_IS(Name)                                                 \
  bool ObjectData::Is##Name() const {                                   \
    if (should_access_heap()) {                                         \
      return i::Is##Name(*object());                                    \
    }                                                                   \
    if (is_smi()) return false;                                         \
    InstanceType instance_type =                                        \
        static_cast<const HeapObjectData*>(this)->GetMapInstanceType(); \
    return InstanceTypeChecker::Is##Name(instance_type);                \
  }
HEAP_BROKER_OBJECT_LIST(DEFINE_IS)
#undef DEFINE_IS

#define DEFINE_AS(Name)                              \
  Name##Data* ObjectData::As##Name() {               \
    CHECK(Is##Name());                               \
    CHECK(kind_ == kBackgroundSerializedHeapObject); \
    return static_cast<Name##Data*>(this);           \
  }
HEAP_BROKER_BACKGROUND_SERIALIZED_OBJECT_LIST(DEFINE_AS)
#undef DEFINE_AS

bool ObjectRef::equals(ObjectRef other) const { return data_ == other.data_; }

ContextRef ContextRef::previous(JSHeapBroker* broker, size_t* depth) const {
  DCHECK_NOT_NULL(depth);

  if (*depth == 0) return *this;

  Tagged<Context> current = *object();
  while (*depth != 0 && i::IsContext(current->unchecked_previous())) {
    current = Cast<Context>(current->unchecked_previous());
    (*depth)--;
  }
  // The `previous` field is immutable after initialization and the
  // context itself is read through an atomic load.
  return MakeRefAssumeMemoryFence(broker, current);
}

OptionalObjectRef ContextRef::get(JSHeapBroker* broker, int index) const {
  CHECK_LE(0, index);
  // Length is immutable after initialization.
  if (index >= object()->length(kRelaxedLoad)) return {};
  return TryMakeRef(broker, object()->get(index));
}

OptionalObjectRef ContextRef::TryGetSideData(JSHeapBroker* broker,
                                             int index) const {
  if (!object()->IsScriptContext()) {
    return {};
  }

  // No side data for slots which are not variables in the context.
  if (index < Context::MIN_CONTEXT_EXTENDED_SLOTS) {
    return {};
  }

  OptionalObjectRef maybe_side_data =
      get(broker, Context::CONST_TRACKING_LET_SIDE_DATA_INDEX);
  if (!maybe_side_data.has_value()) return {};
  // The FixedArray itself will stay constant, but its contents may change while
  // we compile in the background.
  FixedArrayRef side_data_fixed_array = maybe_side_data.value().AsFixedArray();
  return side_data_fixed_array.TryGet(
      broker, index - Context::MIN_CONTEXT_EXTENDED_SLOTS);
}

void JSHeapBroker::InitializeAndStartSerializing(
    DirectHandle<NativeContext> target_native_context) {
  TraceScope tracer(this, "JSHeapBroker::InitializeAndStartSerializing");

  CHECK_EQ(mode_, kDisabled);
  mode_ = kSerializing;

  // Throw away the dummy data that we created while disabled.
  feedback_.clear();
  refs_->Clear();
  refs_ =
      zone()->New<RefsMap>(kInitialRefsBucketCount, AddressMatcher(), zone());

  CollectArrayAndObjectPrototypes();

  SetTargetNativeContextRef(target_native_context);
}

namespace {

constexpr ObjectDataKind ObjectDataKindFor(RefSerializationKind kind) {
  switch (kind) {
    case RefSerializationKind::kBackgroundSerialized:
      return kBackgroundSerializedHeapObject;
    case RefSerializationKind::kNeverSerialized:
      return kNeverSerializedHeapObject;
  }
}

}  // namespace

ObjectData* JSHeapBroker::TryGetOrCreateData(Handle<Object> object,
                                             GetOrCreateDataFlags flags) {
  RefsMap::Entry* entry = refs_->Lookup(object.address());
  if (entry != nullptr) return entry->value;

  if (mode() == JSHeapBroker::kDisabled) {
    entry = refs_->LookupOrInsert(object.address());
    ObjectData** storage = &entry->value;
    if (*storage == nullptr) {
      entry->value = zone()->New<ObjectData>(
          this, storage, object,
          IsSmi(*object) ? kSmi : kUnserializedHeapObject);
    }
    return *storage;
  }

  CHECK(mode() == JSHeapBroker::kSerializing ||
        mode() == JSHeapBroker::kSerialized);

  ObjectData* object_data;
  if (IsSmi(*object)) {
    entry = refs_->LookupOrInsert(object.address());
    return zone()->New<ObjectData>(this, &entry->value, object, kSmi);
  }

  DCHECK(!IsSmi(*object));

  const bool crash_on_error = (flags & kCrashOnError) != 0;

  if ((flags & kAssumeMemoryFence) == 0 &&
      ObjectMayBeUninitialized(Cast<HeapObject>(*object))) {
    TRACE_BROKER_MISSING(this, "Object may be uninitialized " << *object);
    CHECK_WITH_MSG(!crash_on_error, "Ref construction failed");
    return nullptr;
  }

  if (ReadOnlyHeap::SandboxSafeContains(Cast<HeapObject>(*object))) {
    entry = refs_->LookupOrInsert(object.address());
    return zone()->New<ObjectData>(this, &entry->value, object,
                                   kUnserializedReadOnlyHeapObject);
  }

  InstanceType instance_type =
      Cast<HeapObject>(*object)->map()->instance_type();
#define CREATE_DATA(Name)                                             \
  if (i::InstanceTypeChecker::Is##Name(instance_type)) {              \
    entry = refs_->LookupOrInsert(object.address());                  \
    object_data = zone()->New<ref_traits<Name>::data_type>(           \
        this, &entry->value, Cast<Name>(object),                      \
        ObjectDataKindFor(ref_traits<Name>::ref_serialization_kind)); \
    /* NOLINTNEXTLINE(readability/braces) */                          \
  } else
  HEAP_BROKER_OBJECT_LIST(CREATE_DATA)
#undef CREATE_DATA
  {
    UNREACHABLE();
  }

  // Ensure that the original instance type matches the one of the serialized
  // object (if the object was serialized). In particular, this is important
  // for Maps: in GetMapInstanceType we have special handling for maps and will
  // report MAP_TYPE for objects whose map pointer points back to itself. With
  // heap corruption, a non-map object can be made to point to itself though,
  // in which case we may later treat a non-MapData object as a MapData object.
  // See also crbug.com/326700497 for more details.
  if (!object_data->should_access_heap()) {
    SBXCHECK_EQ(
        instance_type,
        static_cast<HeapObjectData*>(object_data)->GetMapInstanceType());
  }

  // At this point the entry pointer is not guaranteed to be valid as
  // the refs_ hash hable could be resized by one of the constructors above.
  DCHECK_EQ(object_data, refs_->Lookup(object.address())->value);
  return object_data;
}

#define DEFINE_IS_AND_AS(Name)                                    \
  bool ObjectRef::Is##Name() const { return data()->Is##Name(); } \
  Name##Ref ObjectRef::As##Name() const {                         \
    DCHECK(Is##Name());                                           \
    return Name##Ref(data());                                     \
  }
HEAP_BROKER_OBJECT_LIST(DEFINE_IS_AND_AS)
#undef DEFINE_IS_AND_AS

bool ObjectRef::IsSmi() const { return data()->is_smi(); }

int ObjectRef::AsSmi() const {
  DCHECK(IsSmi());
  // Handle-dereference is always allowed for Handle<Smi>.
  return Cast<Smi>(*object()).value();
}

#define DEF_TESTER(Type, ...)                              \
  bool MapRef::Is##Type##Map() const {                     \
    return InstanceTypeChecker::Is##Type(instance_type()); \
  }
INSTANCE_TYPE_CHECKERS(DEF_TESTER)
#undef DEF_TESTER

bool MapRef::IsBooleanMap(JSHeapBroker* broker) const {
  return *this == broker->boolean_map();
}

bool MapRef::CanInlineElementAccess() const {
  if (!IsJSObjectMap()) return false;
  if (is_access_check_needed()) return false;
  if (has_indexed_interceptor()) return false;
  ElementsKind kind = elements_kind();
  if (IsFastElementsKind(kind)) return true;
  if (IsTypedArrayElementsKind(kind) &&
      (Is64() || (kind != BIGINT64_ELEMENTS && kind != BIGUINT64_ELEMENTS))) {
    return true;
  }
  if (IsRabGsabTypedArrayElementsKind(kind) &&
      kind != RAB_GSAB_BIGUINT64_ELEMENTS &&
      kind != RAB_GSAB_BIGINT64_ELEMENTS) {
    return true;
  }
  return false;
}

OptionalMapRef MapRef::AsElementsKind(JSHeapBroker* broker,
                                      ElementsKind kind) const {
  const ElementsKind current_kind = elements_kind();
  if (kind == current_kind) return *this;

#ifdef DEBUG
  // If starting from an initial JSArray map, TryAsElementsKind must succeed
  // and return the expected transitioned JSArray map.
  NativeContextRef native_context = broker->target_native_context();
  if (equals(native_context.GetInitialJSArrayMap(broker, current_kind))) {
    // Note that GetInitialJSArrayMap can park the current scope, which can
    // trigger a GC, which means that everything above this point that isn't in
    // a Handle could be invalidated.
    Tagged<Map> initial_js_array_map =
        *native_context.GetInitialJSArrayMap(broker, kind).object();
    Tagged<Map> as_elements_kind_map =
        Map::TryAsElementsKind(broker->isolate(), object(), kind,
                               ConcurrencyMode::kConcurrent)
            .value();
    CHECK_EQ(as_elements_kind_map, initial_js_array_map);
  }
#endif  // DEBUG

  std::optional<Tagged<Map>> maybe_result = Map::TryAsElementsKind(
      broker->isolate(), object(), kind, ConcurrencyMode::kConcurrent);

  if (!maybe_result.has_value()) {
    TRACE_BROKER_MISSING(broker, "MapRef::AsElementsKind " << *this);
    return {};
  }
  return MakeRefAssumeMemoryFence(broker, maybe_result.value());
}

bool MapRef::PrototypesElementsDoNotHaveAccessorsOrThrow(
    JSHeapBroker* broker, ZoneVector<MapRef>* prototype_maps) {
  DCHECK_NOT_NULL(prototype_maps);
  MapRef prototype_map = prototype(broker).map(broker);
  while (prototype_map.oddball_type(broker) != OddballType::kNull) {
    // For the purposes of depending on prototypes' elements behavior when
    // doing keyed property sets, non-extensible and sealed fast elements are
    // okay because they behave like fast elements for stores into holes on
    // the receiver. In such cases, the value is stored on the receiver's
    // elements and does not trigger any setters and does not throw.
    //
    // Note that frozen elements are _not_ okay because of the "override
    // mistake":
    //
    //   Object.prototype[1] = "x";
    //   Object.freeze(Object.prototype);
    //   ([])[1] = "y"; <-- throws in strict mode, nop in sloppy mode
    if (!prototype_map.IsJSObjectMap() || !prototype_map.is_stable() ||
        !IsFastOrNonextensibleOrSealedElementsKind(
            prototype_map.elements_kind())) {
      return false;
    }
    prototype_maps->push_back(prototype_map);
    prototype_map = prototype_map.prototype(broker).map(broker);
  }
  return true;
}

bool MapRef::supports_fast_array_iteration(JSHeapBroker* broker) const {
  return SupportsFastArrayIteration(broker, object());
}

bool MapRef::supports_fast_array_resize(JSHeapBroker* broker) const {
  return SupportsFastArrayResize(broker, object());
}

namespace {

void RecordConsistentJSFunctionViewDependencyIfNeeded(
    const JSHeapBroker* broker, JSFunctionRef ref, JSFunctionData* data,
    JSFunctionData::UsedField used_field) {
  if (!data->has_any_used_field()) {
    // Deduplicate dependencies.
    broker->dependencies()->DependOnConsistentJSFunctionView(ref);
  }
  data->set_used_field(used_field);
}

}  // namespace

OptionalFeedbackVectorRef JSFunctionRef::feedback_vector(
    JSHeapBroker* broker) const {
  return raw_feedback_cell(broker).feedback_vector(broker);
}

int JSFunctionRef::InitialMapInstanceSizeWithMinSlack(
    JSHeapBroker* broker) const {
  if (data_->should_access_heap()) {
    return object()->ComputeInstanceSizeWithMinSlack(broker->isolate());
  }
  RecordConsistentJSFunctionViewDependencyIfNeeded(
      broker, *this, data()->AsJSFunction(),
      JSFunctionData::kInitialMapInstanceSizeWithMinSlack);
  return data()->AsJSFunction()->initial_map_instance_size_with_min_slack();
}

OddballType MapRef::oddball_type(JSHeapBroker* broker) const {
  if (instance_type() != ODDBALL_TYPE) {
    return OddballType::kNone;
  }
  if (equals(broker->undefined_map())) {
    return OddballType::kUndefined;
  }
  if (equals(broker->null_map())) {
    return OddballType::kNull;
  }
  if (equals(broker->boolean_map())) {
    return OddballType::kBoolean;
  }
  UNREACHABLE();
}

FeedbackCellRef FeedbackVectorRef::GetClosureFeedbackCell(JSHeapBroker* broker,
                                                          int index) const {
  return MakeRefAssumeMemoryFence(broker,
                                  object()->closure_feedback_cell(index));
}

OptionalObjectRef JSObjectRef::raw_properties_or_hash(
    JSHeapBroker* broker) const {
  return TryMakeRef(broker, object()->raw_properties_or_hash());
}

OptionalObjectRef JSObjectRef::RawInobjectPropertyAt(JSHeapBroker* broker,
                                                     FieldIndex index) const {
  CHECK(index.is_inobject());
  Handle<Object> value;
  {
    DisallowGarbageCollection no_gc;
    PtrComprCageBase cage_base = broker->cage_base();
    Tagged<Map> current_map = object()->map(cage_base, kAcquireLoad);

    // If the map changed in some prior GC epoch, our {index} could be
    // outside the valid bounds of the cached map.
    if (*map(broker).object() != current_map) {
      TRACE_BROKER_MISSING(broker, "Map change detected in " << *this);
      return {};
    }

    std::optional<Tagged<Object>> maybe_value =
        object()->RawInobjectPropertyAt(cage_base, current_map, index);
    if (!maybe_value.has_value()) {
      TRACE_BROKER_MISSING(broker,
                           "Unable to safely read property in " << *this);
      return {};
    }
    value = broker->CanonicalPersistentHandle(maybe_value.value());
  }
  return TryMakeRef(broker, value);
}

bool JSObjectRef::IsElementsTenured(FixedArrayBaseRef elements) {
  return !ObjectInYoungGeneration(*elements.object());
}

FieldIndex MapRef::GetFieldIndexFor(InternalIndex descriptor_index) const {
  CHECK_LT(descriptor_index.as_int(), NumberOfOwnDescriptors());
  FieldIndex result = FieldIndex::ForDescriptor(*object(), descriptor_index);
  DCHECK(result.is_inobject());
  return result;
}

int MapRef::GetInObjectPropertyOffset(int i) const {
  return object()->GetInObjectPropertyOffset(i);
}

PropertyDetails MapRef::GetPropertyDetails(
    JSHeapBroker* broker, InternalIndex descriptor_index) const {
  CHECK_LT(descriptor_index.as_int(), NumberOfOwnDescriptors());
  return instance_descriptors(broker).GetPropertyDetails(descriptor_index);
}

NameRef MapRef::GetPropertyKey(JSHeapBroker* broker,
                               InternalIndex descriptor_index) const {
  CHECK_LT(descriptor_index.as_int(), NumberOfOwnDescriptors());
  return instance_descriptors(broker).GetPropertyKey(broker, descriptor_index);
}

bool MapRef::IsFixedCowArrayMap(JSHeapBroker* broker) const {
  Handle<Map> fixed_cow_array_map =
      ReadOnlyRoots(broker->isolate()).fixed_cow_array_map_handle();
  return equals(MakeRef(broker, fixed_cow_array_map));
}

bool MapRef::IsPrimitiveMap() const {
  return instance_type() <= LAST_PRIMITIVE_HEAP_OBJECT_TYPE;
}

MapRef MapRef::FindFieldOwner(JSHeapBroker* broker,
                              InternalIndex descriptor_index) const {
  CHECK_LT(descriptor_index.as_int(), NumberOfOwnDescriptors());
  // TODO(solanes, v8:7790): Consider caching the result of the field owner on
  // the descriptor array. It would be useful for same map as well as any
  // other map sharing that descriptor array.
  return MakeRefAssumeMemoryFence(
      broker, object()->FindFieldOwner(broker->cage_base(), descriptor_index));
}

OptionalObjectRef StringRef::GetCharAsStringOrUndefined(JSHeapBroker* broker,
                                                        uint32_t index) const {
  Tagged<String> maybe_char;
  auto result = ConcurrentLookupIterator::TryGetOwnChar(
      &maybe_char, broker->isolate(), broker->local_isolate(), *object(),
      index);

  if (result == ConcurrentLookupIterator::kGaveUp) {
    TRACE_BROKER_MISSING(broker, "StringRef::GetCharAsStringOrUndefined on "
                                     << *this << " at index " << index);
    return {};
  }

  DCHECK_EQ(result, ConcurrentLookupIterator::kPresent);
  return TryMakeRef(broker, maybe_char);
}

bool StringRef::SupportedStringKind() const {
  return IsInternalizedString() || IsThinString(*object());
}

bool StringRef::IsContentAccessible() const {
  return data_->kind() != kNeverSerializedHeapObject || SupportedStringKind();
}

bool StringRef::IsOneByteRepresentation() const {
  return object()->IsOneByteRepresentation();
}

// TODO(leszeks): The broker is only needed here for tracing, maybe we could get
// it from a thread local instead.
std::optional<Handle<String>> StringRef::ObjectIfContentAccessible(
    JSHeapBroker* broker) {
  if (!IsContentAccessible()) {
    TRACE_BROKER_MISSING(
        broker,
        "content for kNeverSerialized unsupported string kind " << *this);
    return std::nullopt;
  } else {
    return object();
  }
}

int StringRef::length() const { return object()->length(kAcquireLoad); }

std::optional<uint16_t> StringRef::GetFirstChar(JSHeapBroker* broker) const {
  return GetChar(broker, 0);
}

std::optional<uint16_t> StringRef::GetChar(JSHeapBroker* broker,
                                           int index) const {
  if (!IsContentAccessible()) {
    TRACE_BROKER_MISSING(
        broker,
        "get char for kNeverSerialized unsupported string kind " << *this);
    return std::nullopt;
  }

  if (!broker->IsMainThread()) {
    return object()->Get(index, broker->local_isolate());
  } else {
    // TODO(solanes, v8:7790): Remove this case once the inlining phase is
    // done concurrently all the time.
    return object()->Get(index);
  }
}

std::optional<double> StringRef::ToNumber(JSHeapBroker* broker) {
  if (!IsContentAccessible()) {
    TRACE_BROKER_MISSING(
        broker,
        "number for kNeverSerialized unsupported string kind " << *this);
    return std::nullopt;
  }

  return TryStringToDouble(broker->local_isolate(), object());
}

std::optional<double> StringRef::ToInt(JSHeapBroker* broker, int radix) {
  if (!IsContentAccessible()) {
    TRACE_BROKER_MISSING(
        broker, "toInt for kNeverSerialized unsupported string kind " << *this);
    return std::nullopt;
  }

  return TryStringToInt(broker->local_isolate(), object(), radix);
}

int ArrayBoilerplateDescriptionRef::constants_elements_length() const {
  return object()->constant_elements()->length();
}

OptionalObjectRef FixedArrayRef::TryGet(JSHeapBroker* broker, int i) const {
  Handle<Object> value;
  {
    DisallowGarbageCollection no_gc;
    CHECK_GE(i, 0);
    value = broker->CanonicalPersistentHandle(object()->get(i, kAcquireLoad));
    if (i >= object()->length(kAcquireLoad)) {
      // Right-trimming happened.
      CHECK_LT(i, length());
      return {};
    }
  }
  return TryMakeRef(broker, value);
}

Float64 FixedDoubleArrayRef::GetFromImmutableFixedDoubleArray(int i) const {
  static_assert(ref_traits<FixedDoubleArray>::ref_serialization_kind ==
                RefSerializationKind::kNeverSerialized);
  CHECK(data_->should_access_heap());
  return Float64::FromBits(object()->get_representation(i));
}

Handle<TrustedByteArray> BytecodeArrayRef::SourcePositionTable(
    JSHeapBroker* broker) const {
  return broker->CanonicalPersistentHandle(object()->SourcePositionTable());
}

Address BytecodeArrayRef::handler_table_address() const {
  return reinterpret_cast<Address>(object()->handler_table()->begin());
}

int BytecodeArrayRef::handler_table_size() const {
  return object()->handler_table()->length();
}

#define IF_ACCESS_FROM_HEAP_C(name)  \
  if (data_->should_access_heap()) { \
    return object()->name();         \
  }

#define IF_ACCESS_FROM_HEAP(result, name)                   \
  if (data_->should_access_heap()) {                        \
    return MakeRef(broker, Cast<result>(object()->name())); \
  }

// Macros for definining a const getter that, depending on the data kind,
// either looks into the heap or into the serialized data.
#define BIMODAL_ACCESSOR(holder, result, name)                   \
  result##Ref holder##Ref::name(JSHeapBroker* broker) const {    \
    IF_ACCESS_FROM_HEAP(result, name);                           \
    return result##Ref(ObjectRef::data()->As##holder()->name()); \
  }

// Like above except that the result type is not an XYZRef.
#define BIMODAL_ACCESSOR_C(holder, result, name)    \
  result holder##Ref::name() const {                \
    IF_ACCESS_FROM_HEAP_C(name);                    \
    return ObjectRef::data()->As##holder()->name(); \
  }

// Like above but for BitFields.
#define BIMODAL_ACCESSOR_B(holder, field, name, BitField)              \
  typename BitField::FieldType holder##Ref::name() const {             \
    IF_ACCESS_FROM_HEAP_C(name);                                       \
    return BitField::decode(ObjectRef::data()->As##holder()->field()); \
  }

#define HEAP_ACCESSOR_C(holder, result, name) \
  result holder##Ref::name() const { return object()->name(); }

#define HEAP_ACCESSOR_B(holder, field, name, BitField)     \
  typename BitField::FieldType holder##Ref::name() const { \
    return object()->name();                               \
  }

ObjectRef AllocationSiteRef::nested_site(JSHeapBroker* broker) const {
  return MakeRefAssumeMemoryFence(broker, object()->nested_site());
}

HEAP_ACCESSOR_C(AllocationSite, bool, CanInlineCall)
HEAP_ACCESSOR_C(AllocationSite, bool, PointsToLiteral)
HEAP_ACCESSOR_C(AllocationSite, ElementsKind, GetElementsKind)
HEAP_ACCESSOR_C(AllocationSite, AllocationType, GetAllocationType)

BIMODAL_ACCESSOR_C(BigInt, uint64_t, AsUint64)
int64_t BigIntRef::AsInt64(bool* lossless) const {
  if (data_->should_access_heap()) {
    return object()->AsInt64(lossless);
  }
  return ObjectRef::data()->AsBigInt()->AsInt64(lossless);
}

int BytecodeArrayRef::length() const { return object()->length(); }
int BytecodeArrayRef::register_count() const {
  return object()->register_count();
}
uint16_t BytecodeArrayRef::parameter_count() const {
  return object()->parameter_count();
}
uint16_t BytecodeArrayRef::max_arguments() const {
  return object()->max_arguments();
}
interpreter::Register
BytecodeArrayRef::incoming_new_target_or_generator_register() const {
  return object()->incoming_new_target_or_generator_register();
}

BIMODAL_ACCESSOR(HeapObject, Map, map)

HEAP_ACCESSOR_C(HeapNumber, double, value)

uint64_t HeapNumberRef::value_as_bits() const {
  return object()->value_as_bits();
}

JSReceiverRef JSBoundFunctionRef::bound_target_function(
    JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(broker, object()->bound_target_function());
}

ObjectRef JSBoundFunctionRef::bound_this(JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(broker, object()->bound_this());
}

FixedArrayRef JSBoundFunctionRef::bound_arguments(JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(broker, object()->bound_arguments());
}

// Immutable after initialization.
HEAP_ACCESSOR_C(JSDataView, size_t, byte_length)

HEAP_ACCESSOR_B(Map, bit_field2, elements_kind, Map::Bits2::ElementsKindBits)
HEAP_ACCESSOR_B(Map, bit_field3, is_dictionary_map,
                Map::Bits3::IsDictionaryMapBit)
HEAP_ACCESSOR_B(Map, bit_field3, is_deprecated, Map::Bits3::IsDeprecatedBit)
HEAP_ACCESSOR_B(Map, bit_field3, NumberOfOwnDescriptors,
                Map::Bits3::NumberOfOwnDescriptorsBits)
HEAP_ACCESSOR_B(Map, bit_field3, is_migration_target,
                Map::Bits3::IsMigrationTargetBit)
BIMODAL_ACCESSOR_B(Map, bit_field3, construction_counter,
                   Map::Bits3::ConstructionCounterBits)
HEAP_ACCESSOR_B(Map, bit_field, has_prototype_slot,
                Map::Bits1::HasPrototypeSlotBit)
HEAP_ACCESSOR_B(Map, bit_field, is_access_check_needed,
                Map::Bits1::IsAccessCheckNeededBit)
HEAP_ACCESSOR_B(Map, bit_field, is_callable, Map::Bits1::IsCallableBit)
HEAP_ACCESSOR_B(Map, bit_field, has_indexed_interceptor,
                Map::Bits1::HasIndexedInterceptorBit)
HEAP_ACCESSOR_B(Map, bit_field, is_constructor, Map::Bits1::IsConstructorBit)
HEAP_ACCESSOR_B(Map, bit_field, is_undetectable, Map::Bits1::IsUndetectableBit)
BIMODAL_ACCESSOR_C(Map, int, instance_size)
HEAP_ACCESSOR_C(Map, int, NextFreePropertyIndex)
BIMODAL_ACCESSOR_C(Map, int, UnusedPropertyFields)
HEAP_ACCESSOR_C(Map, InstanceType, instance_type)
BIMODAL_ACCESSOR_C(Map, bool, is_abandoned_prototype_map)

int ObjectBoilerplateDescriptionRef::boilerplate_properties_count() const {
  return object()->boilerplate_properties_count();
}

BIMODAL_ACCESSOR(PropertyCell, Object, value)
BIMODAL_ACCESSOR_C(PropertyCell, PropertyDetails, property_details)

HeapObjectRef RegExpBoilerplateDescriptionRef::data(
    JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(
      broker, Cast<HeapObject>(object()->data(broker->isolate())));
}

StringRef RegExpBoilerplateDescriptionRef::source(JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(broker, object()->source());
}

int RegExpBoilerplateDescriptionRef::flags() const { return object()->flags(); }

Address FunctionTemplateInfoRef::callback(JSHeapBroker* broker) const {
  return object()->callback(broker->isolate());
}

OptionalObjectRef FunctionTemplateInfoRef::callback_data(
    JSHeapBroker* broker) const {
  ObjectRef data =
      MakeRefAssumeMemoryFence(broker, object()->callback_data(kAcquireLoad));
  if (data.IsTheHole()) return {};
  return data;
}

bool FunctionTemplateInfoRef::is_signature_undefined(
    JSHeapBroker* broker) const {
  return i::IsUndefined(object()->signature(), broker->isolate());
}

HEAP_ACCESSOR_C(FunctionTemplateInfo, bool, accept_any_receiver)
HEAP_ACCESSOR_C(FunctionTemplateInfo, int16_t,
                allowed_receiver_instance_type_range_start)
HEAP_ACCESSOR_C(FunctionTemplateInfo, int16_t,
                allowed_receiver_instance_type_range_end)

HolderLookupResult FunctionTemplateInfoRef::LookupHolderOfExpectedType(
    JSHeapBroker* broker, MapRef receiver_map) {
  const HolderLookupResult not_found;
  if (!receiver_map.IsJSObjectMap() || (receiver_map.is_access_check_needed() &&
                                        !object()->accept_any_receiver())) {
    return not_found;
  }

  DirectHandle<FunctionTemplateInfo> expected_receiver_type;
  {
    DisallowGarbageCollection no_gc;
    Tagged<HeapObject> signature = object()->signature();
    if (i::IsUndefined(signature)) {
      return HolderLookupResult(CallOptimization::kHolderIsReceiver);
    }
    expected_receiver_type = broker->CanonicalPersistentHandle(
        Cast<FunctionTemplateInfo>(signature));
    if (expected_receiver_type->IsTemplateFor(*receiver_map.object())) {
      return HolderLookupResult(CallOptimization::kHolderIsReceiver);
    }
    if (!receiver_map.IsJSGlobalProxyMap()) return not_found;
  }

  HeapObjectRef prototype = receiver_map.prototype(broker);
  if (prototype.IsNull()) return not_found;
  if (!expected_receiver_type->IsTemplateFor(prototype.object()->map())) {
    return not_found;
  }
  CHECK(prototype.IsJSObject());
  return HolderLookupResult(CallOptimization::kHolderFound,
                            prototype.AsJSObject());
}

HEAP_ACCESSOR_C(ScopeInfo, int, ContextLength)
HEAP_ACCESSOR_C(ScopeInfo, bool, HasContextExtensionSlot)
HEAP_ACCESSOR_C(ScopeInfo, bool, HasOuterScopeInfo)
HEAP_ACCESSOR_C(ScopeInfo, bool, HasContext)
HEAP_ACCESSOR_C(ScopeInfo, bool, ClassScopeHasPrivateBrand)
HEAP_ACCESSOR_C(ScopeInfo, ScopeType, scope_type)

ScopeInfoRef ScopeInfoRef::OuterScopeInfo(JSHeapBroker* broker) const {
  return MakeRefAssumeMemoryFence(broker, object()->OuterScopeInfo());
}

HEAP_ACCESSOR_C(SharedFunctionInfo, Builtin, builtin_id)

BytecodeArrayRef SharedFunctionInfoRef::GetBytecodeArray(
    JSHeapBroker* broker) const {
  CHECK(HasBytecodeArray());
  Tagged<BytecodeArray> bytecode_array;
  if (!broker->IsMainThread()) {
    bytecode_array = object()->GetBytecodeArray(broker->local_isolate());
  } else {
    bytecode_array = object()->GetBytecodeArray(broker->isolate());
  }
  return MakeRefAssumeMemoryFence(broker, bytecode_array);
}

#define DEF_SFI_ACCESSOR(type, name) \
  HEAP_ACCESSOR_C(SharedFunctionInfo, type, name)
BROKER_SFI_FIELDS(DEF_SFI_ACCESSOR)
#undef DEF_SFI_ACCESSOR

bool SharedFunctionInfoRef::HasBreakInfo(JSHeapBroker* broker) const {
  if (broker->IsMainThread()) {
    return object()->HasBreakInfo(broker->isolate());
  } else {
    LocalIsolate* local_isolate = broker->local_isolate();
    SharedMutexGuardIfOffThread<LocalIsolate, base::kShared> mutex_guard(
        local_isolate->shared_function_info_access(), local_isolate);
    return object()->HasBreakInfo(local_isolate->GetMainThreadIsolateUnsafe());
  }
}

SharedFunctionInfo::Inlineability SharedFunctionInfoRef::GetInlineability(
    JSHeapBroker* broker) const {
  return broker->IsMainThread()
             ? object()->GetInlineability(broker->isolate())
             : object()->GetInlineability(broker->local_isolate());
}

ObjectRef FeedbackCellRef::value(JSHeapBroker* broker) const {
  DCHECK(data_->should_access_heap());
  return MakeRefAssumeMemoryFence(broker, object()->value(kAcquireLoad));
}

bool FeedbackVectorRef::was_once_deoptimized() const {
  return object()->was_once_deoptimized();
}

OptionalObjectRef MapRef::GetStrongValue(JSHeapBroker* broker,
                                         InternalIndex descriptor_index) const {
  CHECK_LT(descriptor_index.as_int(), NumberOfOwnDescriptors());
  return instance_descriptors(broker).GetStrongValue(broker, descriptor_index);
}

DescriptorArrayRef MapRef::instance_descriptors(JSHeapBroker* broker) const {
  return MakeRefAssumeMemoryFence(
      broker,
      object()->instance_descriptors(broker->cage_base(), kAcquireLoad));
}

HeapObjectRef MapRef::prototype(JSHeapBroker* broker) const {
  return MakeRefAssumeMemoryFence(broker,
                                  Cast<HeapObject>(object()->prototype()));
}

MapRef MapRef::FindRootMap(JSHeapBroker* broker) const {
  // TODO(solanes, v8:7790): Consider caching the result of the root map.
  return MakeRefAssumeMemoryFence(broker,
                                  object()->FindRootMap(broker->cage_base()));
}

ObjectRef MapRef::GetConstructor(JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(broker, object()->GetConstructor());
}

HeapObjectRef MapRef::GetBackPointer(JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(broker,
                                  Cast<HeapObject>(object()->GetBackPointer()));
}

bool JSTypedArrayRef::is_on_heap() const {
  // Underlying field written 1. during initialization or 2. with release-store.
  return object()->is_on_heap(kAcquireLoad);
}

size_t JSTypedArrayRef::length() const {
  CHECK(!is_on_heap());
  // Immutable after initialization.
  return object()->length();
}

HeapObjectRef JSTypedArrayRef::buffer(JSHeapBroker* broker) const {
  CHECK(!is_on_heap());
  // Immutable after initialization.
  return MakeRef<HeapObject>(broker, object()->buffer());
}

void* JSTypedArrayRef::data_ptr() const {
  CHECK(!is_on_heap());
  // Underlying field written 1. during initialization or 2. protected by the
  // is_on_heap release/acquire semantics (external_pointer store happens-before
  // base_pointer store, and this external_pointer load happens-after
  // base_pointer load).
  static_assert(JSTypedArray::kOffHeapDataPtrEqualsExternalPointer);
  return object()->DataPtr();
}

bool JSPrimitiveWrapperRef::IsStringWrapper(JSHeapBroker* broker) const {
  auto elements_kind = map(broker).elements_kind();
  return elements_kind == FAST_STRING_WRAPPER_ELEMENTS ||
         elements_kind == SLOW_STRING_WRAPPER_ELEMENTS;
}

bool MapRef::IsInobjectSlackTrackingInProgress() const {
  return construction_counter() != Map::kNoSlackTracking;
}

int MapRef::constructor_function_index() const {
  return object()->GetConstructorFunctionIndex();
}

bool MapRef::is_stable() const {
  IF_ACCESS_FROM_HEAP_C(is_stable);
  return !Map::Bits3::IsUnstableBit::decode(data()->AsMap()->bit_field3());
}

bool MapRef::CanBeDeprecated() const { return object()->CanBeDeprecated(); }

bool MapRef::CanTransition() const { return object()->CanTransition(); }

int MapRef::GetInObjectPropertiesStartInWords() const {
  return object()->GetInObjectPropertiesStartInWords();
}

int MapRef::GetInObjectProperties() const {
  IF_ACCESS_FROM_HEAP_C(GetInObjectProperties);
  return data()->AsMap()->in_object_properties();
}

bool StringRef::IsExternalString() const {
  return i::IsExternalString(*object());
}

ZoneVector<Address> FunctionTemplateInfoRef::c_functions(
    JSHeapBroker* broker) const {
  return GetCFunctions(Cast<FixedArray>(object()->GetCFunctionOverloads()),
                       broker->isolate(), broker->zone());
}

ZoneVector<const CFunctionInfo*> FunctionTemplateInfoRef::c_signatures(
    JSHeapBroker* broker) const {
  return GetCSignatures(Cast<FixedArray>(object()->GetCFunctionOverloads()),
                        broker->isolate(), broker->zone());
}

bool StringRef::IsSeqString() const { return i::IsSeqString(*object()); }

ScopeInfoRef ContextRef::scope_info(JSHeapBroker* broker) const {
  // The scope_info is immutable after initialization.
  return MakeRefAssumeMemoryFence(broker, object()->scope_info());
}

MapRef NativeContextRef::GetFunctionMapFromIndex(JSHeapBroker* broker,
                                                 int index) const {
  DCHECK_GE(index, Context::FIRST_FUNCTION_MAP_INDEX);
  DCHECK_LE(index, Context::LAST_FUNCTION_MAP_INDEX);
  CHECK_LT(index, object()->length());
  return MakeRefAssumeMemoryFence(
      broker, Cast<Map>(object()->get(index, kAcquireLoad)));
}

MapRef NativeContextRef::GetInitialJSArrayMap(JSHeapBroker* broker,
                                              ElementsKind kind) const {
  switch (kind) {
    case PACKED_SMI_ELEMENTS:
      return js_array_packed_smi_elements_map(broker);
    case HOLEY_SMI_ELEMENTS:
      return js_array_holey_smi_elements_map(broker);
    case PACKED_DOUBLE_ELEMENTS:
      return js_array_packed_double_elements_map(broker);
    case HOLEY_DOUBLE_ELEMENTS:
      return js_array_holey_double_elements_map(broker);
    case PACKED_ELEMENTS:
      return js_array_packed_elements_map(broker);
    case HOLEY_ELEMENTS:
      return js_array_holey_elements_map(broker);
    default:
      UNREACHABLE();
  }
}

#define DEF_NATIVE_CONTEXT_ACCESSOR(ResultType, Name)                  \
  ResultType##Ref NativeContextRef::Name(JSHeapBroker* broker) const { \
    return MakeRefAssumeMemoryFence(                                   \
        broker, Cast<ResultType>(object()->Name(kAcquireLoad)));       \
  }
BROKER_NATIVE_CONTEXT_FIELDS(DEF_NATIVE_CONTEXT_ACCESSOR)
#undef DEF_NATIVE_CONTEXT_ACCESSOR

OptionalJSFunctionRef NativeContextRef::GetConstructorFunction(
    JSHeapBroker* broker, MapRef map) const {
  CHECK(map.IsPrimitiveMap());
  switch (map.constructor_function_index()) {
    case Map::kNoConstructorFunctionIndex:
      return std::nullopt;
    case Context::BIGINT_FUNCTION_INDEX:
      return bigint_function(broker);
    case Context::BOOLEAN_FUNCTION_INDEX:
      return boolean_function(broker);
    case Context::NUMBER_FUNCTION_INDEX:
      return number_function(broker);
    case Context::STRING_FUNCTION_INDEX:
      return string_function(broker);
    case Context::SYMBOL_FUNCTION_INDEX:
      return symbol_function(broker);
    default:
      UNREACHABLE();
  }
}

bool ObjectRef::IsNull() const { return i::IsNull(*object()); }

bool ObjectRef::IsUndefined() const { return i::IsUndefined(*object()); }

bool ObjectRef::IsTheHole() const {
  if (i::IsTheHole(*object())) return true;
  DCHECK(!i::IsHole(*object()));
  return false;
}

bool ObjectRef::IsPropertyCellHole() const {
  if (i::IsPropertyCellHole(*object())) return true;
  DCHECK(!i::IsHole(*object()));
  return false;
}

bool ObjectRef::IsHashTableHole() const {
  if (i::IsHashTableHole(*object())) return true;
  DCHECK(!i::IsHole(*object()));
  return false;
}

HoleType ObjectRef::HoleType() const {
  // Trusted objects cannot be TheHole and comparing them to TheHole is not
  // allowed, as they live in different cage bases.
  if (i::IsHeapObject(*object()) &&
      IsTrustedSpaceObject(Cast<HeapObject>(*object())))
    return HoleType::kNone;
#define IF_HOLE_THEN_RETURN(Name, name, Root) \
  if (i::Is##Name(*object())) {               \
    return HoleType::k##Name;                 \
  }

  HOLE_LIST(IF_HOLE_THEN_RETURN)
#undef IF_HOLE_THEN_RETURN

  return HoleType::kNone;
}

bool ObjectRef::IsNullOrUndefined() const { return IsNull() || IsUndefined(); }

std::optional<bool> ObjectRef::TryGetBooleanValue(JSHeapBroker* broker) const {
  if (data_->should_access_heap()) {
    return Object::BooleanValue(*object(), broker->isolate());
  }
  if (IsSmi()) return AsSmi() != 0;
  return data()->AsHeapObject()->TryGetBooleanValue(broker);
}

Maybe<double> ObjectRef::OddballToNumber(JSHeapBroker* broker) const {
  OddballType type = AsHeapObject().map(broker).oddball_type(broker);

  switch (type) {
    case OddballType::kBoolean: {
      ObjectRef true_ref = broker->true_value();
      return this->equals(true_ref) ? Just(1.0) : Just(0.0);
    }
    case OddballType::kUndefined: {
      return Just(std::numeric_limits<double>::quiet_NaN());
    }
    case OddballType::kNull: {
      return Just(0.0);
    }
    default: {
      return Nothing<double>();
    }
  }
}

bool ObjectRef::should_access_heap() const {
  return data()->should_access_heap();
}

OptionalObjectRef JSObjectRef::GetOwnConstantElement(
    JSHeapBroker* broker, FixedArrayBaseRef elements_ref, uint32_t index,
    CompilationDependencies* dependencies) const {
  std::optional<Tagged<Object>> maybe_element = GetOwnConstantElementFromHeap(
      broker, *elements_ref.object(), map(broker).elements_kind(), index);
  if (!maybe_element.has_value()) return {};

  OptionalObjectRef result = TryMakeRef(broker, maybe_element.value());
  if (result.has_value()) {
    dependencies->DependOnOwnConstantElement(*this, index, *result);
  }
  return result;
}

std::optional<Tagged<Object>> JSObjectRef::GetOwnConstantElementFromHeap(
    JSHeapBroker* broker, Tagged<FixedArrayBase> elements,
    ElementsKind elements_kind, uint32_t index) const {
  DCHECK_LE(index, JSObject::kMaxElementIndex);

  DirectHandle<JSObject> holder = object();

  // This block is carefully constructed to avoid Ref creation and access since
  // this method may be called after the broker has retired.
  // The relaxed `length` read is safe to use in this case since:
  // - TryGetOwnConstantElement (below) only detects a constant for JSArray
  //   holders if the array is frozen.
  // - Frozen arrays can't change length.
  // - We've already seen the corresponding map (when this JSObjectRef was
  //   created);
  // - The release-load of that map ensures we read the newest value
  //   of `length` below.
  if (i::IsJSArray(*holder)) {
    Tagged<Object> array_length_obj =
        Cast<JSArray>(*holder)->length(broker->isolate(), kRelaxedLoad);
    if (!i::IsSmi(array_length_obj)) {
      // Can't safely read into HeapNumber objects without atomic semantics
      // (relaxed would be sufficient due to the guarantees above).
      return {};
    }
    uint32_t array_length;
    if (!Object::ToArrayLength(array_length_obj, &array_length)) {
      return {};
    }
    // See also ElementsAccessorBase::GetMaxIndex.
    if (index >= array_length) return {};
  }

  Tagged<Object> maybe_element;
  auto result = ConcurrentLookupIterator::TryGetOwnConstantElement(
      &maybe_element, broker->isolate(), broker->local_isolate(), *holder,
      elements, elements_kind, index);

  if (result == ConcurrentLookupIterator::kGaveUp) {
    TRACE_BROKER_MISSING(broker, "JSObject::GetOwnConstantElement on "
                                     << *this << " at index " << index);
    return {};
  } else if (result == ConcurrentLookupIterator::kNotPresent) {
    return {};
  }

  DCHECK_EQ(result, ConcurrentLookupIterator::kPresent);
  return maybe_element;
}

OptionalObjectRef JSObjectRef::GetOwnFastConstantDataProperty(
    JSHeapBroker* broker, Representation field_representation, FieldIndex index,
    CompilationDependencies* dependencies) const {
  // Use GetOwnFastConstantDoubleProperty for doubles.
  DCHECK(!field_representation.IsDouble());

  std::optional<Tagged<Object>> constant =
      GetOwnFastConstantDataPropertyFromHeap(broker, *this,
                                             field_representation, index);
  if (!constant) return {};

  OptionalObjectRef result =
      TryMakeRef(broker, broker->CanonicalPersistentHandle(constant.value()));

  if (!result.has_value()) return {};

  dependencies->DependOnOwnConstantDataProperty(*this, map(broker), index,
                                                *result);
  return result;
}

std::optional<Float64> JSObjectRef::GetOwnFastConstantDoubleProperty(
    JSHeapBroker* broker, FieldIndex index,
    CompilationDependencies* dependencies) const {
  std::optional<Tagged<Object>> constant =
      GetOwnFastConstantDataPropertyFromHeap(broker, *this,
                                             Representation::Double(), index);
  if (!constant) return {};

  DCHECK(i::IsHeapNumber(constant.value()));

  Float64 unboxed_value = Float64::FromBits(
      RacyReadHeapNumberBits(Cast<HeapNumber>(constant.value())));

  dependencies->DependOnOwnConstantDoubleProperty(*this, map(broker), index,
                                                  unboxed_value);
  return unboxed_value;
}

OptionalObjectRef JSObjectRef::GetOwnDictionaryProperty(
    JSHeapBroker* broker, InternalIndex index,
    CompilationDependencies* dependencies) const {
  CHECK(index.is_found());
  OptionalObjectRef result =
      GetOwnDictionaryPropertyFromHeap(broker, object(), index);
  if (result.has_value()) {
    dependencies->DependOnOwnConstantDictionaryProperty(*this, index, *result);
  }
  return result;
}

ObjectRef JSArrayRef::GetBoilerplateLength(JSHeapBroker* broker) const {
  // Safe to read concurrently because:
  // - boilerplates are immutable after initialization.
  // - boilerplates are published into the feedback vector.
  // These facts also mean we can expect a valid value.
  return length_unsafe(broker).value();
}

OptionalObjectRef JSArrayRef::length_unsafe(JSHeapBroker* broker) const {
  return TryMakeRef(broker, object()->length(broker->isolate(), kRelaxedLoad));
}

OptionalObjectRef JSArrayRef::GetOwnCowElement(JSHeapBroker* broker,
                                               FixedArrayBaseRef elements_ref,
                                               uint32_t index) const {
  // Note: we'd like to check `elements_ref == elements()` here, but due to
  // concurrency this may not hold. The code below must be able to deal with
  // concurrent `elements` modifications.

  // Due to concurrency, the kind read here may not be consistent with
  // `elements_ref`. The caller has to guarantee consistency at runtime by
  // other means (e.g. through a runtime equality check or a compilation
  // dependency).
  ElementsKind elements_kind = map(broker).elements_kind();

  // We only inspect fixed COW arrays, which may only occur for fast
  // smi/objects elements kinds.
  if (!IsSmiOrObjectElementsKind(elements_kind)) return {};
  DCHECK(IsFastElementsKind(elements_kind));
  if (!elements_ref.map(broker).IsFixedCowArrayMap(broker)) return {};

  // As the name says, the `length` read here is unsafe and may not match
  // `elements`. We rely on the invariant that any `length` change will
  // also result in an `elements` change to make this safe. The `elements`
  // consistency check in the caller thus also guards the value of `length`.
  OptionalObjectRef length_ref = length_unsafe(broker);

  if (!length_ref.has_value()) return {};

  // Likewise we only deal with smi lengths.
  if (!length_ref->IsSmi()) return {};

  std::optional<Tagged<Object>> result =
      ConcurrentLookupIterator::TryGetOwnCowElement(
          broker->isolate(), *elements_ref.AsFixedArray().object(),
          elements_kind, length_ref->AsSmi(), index);
  if (!result.has_value()) return {};

  return TryMakeRef(broker, result.value());
}

OptionalCellRef SourceTextModuleRef::GetCell(JSHeapBroker* broker,
                                             int cell_index) const {
  return TryMakeRef(broker, object()->GetCell(cell_index));
}

OptionalObjectRef SourceTextModuleRef::import_meta(JSHeapBroker* broker) const {
  return TryMakeRef(broker, object()->import_meta(kAcquireLoad));
}

OptionalMapRef HeapObjectRef::map_direct_read(JSHeapBroker* broker) const {
  PtrComprCageBase cage_base = broker->cage_base();
  return TryMakeRef(broker, object()->map(cage_base, kAcquireLoad),
                    kAssumeMemoryFence);
}

namespace {

OddballType GetOddballType(Isolate* isolate, Tagged<Map> map) {
  if (map->instance_type() != ODDBALL_TYPE) {
    return OddballType::kNone;
  }
  ReadOnlyRoots roots(isolate);
  if (map == roots.undefined_map()) {
    return OddballType::kUndefined;
  }
  if (map == roots.null_map()) {
    return OddballType::kNull;
  }
  if (map == roots.boolean_map()) {
    return OddballType::kBoolean;
  }
  UNREACHABLE();
}

}  // namespace

HeapObjectType HeapObjectRef::GetHeapObjectType(JSHeapBroker* broker) const {
  if (data_->should_access_heap()) {
    Tagged<Map> map = Cast<HeapObject>(object())->map(broker->cage_base());
    HeapObjectType::Flags flags(0);
    if (map->is_undetectable()) flags |= HeapObjectType::kUndetectable;
    if (map->is_callable()) flags |= HeapObjectType::kCallable;
    return HeapObjectType(map->instance_type(), map->elements_kind(), flags,
                          GetOddballType(broker->isolate(), map), HoleType());
  }
  HeapObjectType::Flags flags(0);
  if (map(broker).is_undetectable()) flags |= HeapObjectType::kUndetectable;
  if (map(broker).is_callable()) flags |= HeapObjectType::kCallable;
  return HeapObjectType(map(broker).instance_type(),
                        map(broker).elements_kind(), flags,
                        map(broker).oddball_type(broker), HoleType());
}

OptionalJSObjectRef AllocationSiteRef::boilerplate(JSHeapBroker* broker) const {
  if (!PointsToLiteral()) return {};
  DCHECK(data_->should_access_heap());
  return TryMakeRef(broker, object()->boilerplate(kAcquireLoad));
}

OptionalFixedArrayBaseRef JSObjectRef::elements(JSHeapBroker* broker,
                                                RelaxedLoadTag tag) const {
  return TryMakeRef(broker, object()->elements(tag));
}

int FixedArrayBaseRef::length() const {
  IF_ACCESS_FROM_HEAP_C(length);
  return data()->AsFixedArrayBase()->length();
}

PropertyDetails DescriptorArrayRef::GetPropertyDetails(
    InternalIndex descriptor_index) const {
  return object()->GetDetails(descriptor_index);
}

NameRef DescriptorArrayRef::GetPropertyKey(
    JSHeapBroker* broker, InternalIndex descriptor_index) const {
  NameRef result = MakeRef(broker, object()->GetKey(descriptor_index));
  CHECK(result.IsUniqueName());
  return result;
}

OptionalObjectRef DescriptorArrayRef::GetStrongValue(
    JSHeapBroker* broker, InternalIndex descriptor_index) const {
  Tagged<HeapObject> heap_object;
  if (!object()
           ->GetValue(descriptor_index)
           .GetHeapObjectIfStrong(&heap_object)) {
    return {};
  }
  // Since the descriptors in the descriptor array can be changed in-place
  // via DescriptorArray::Replace, we might get a value that we haven't seen
  // before.
  return TryMakeRef(broker, heap_object);
}

OptionalFeedbackVectorRef FeedbackCellRef::feedback_vector(
    JSHeapBroker* broker) const {
  ObjectRef contents = value(broker);
  if (!contents.IsFeedbackVector()) return {};
  return contents.AsFeedbackVector();
}

OptionalSharedFunctionInfoRef FeedbackCellRef::shared_function_info(
    JSHeapBroker* broker) const {
  OptionalFeedbackVectorRef vector = feedback_vector(broker);
  if (!vector.has_value()) return {};
  return vector->shared_function_info(broker);
}

SharedFunctionInfoRef FeedbackVectorRef::shared_function_info(
    JSHeapBroker* broker) const {
  // Immutable after initialization.
  return MakeRefAssumeMemoryFence(broker, object()->shared_function_info());
}

bool NameRef::IsUniqueName() const {
  // Must match Name::IsUniqueName.
  return IsInternalizedString() || IsSymbol();
}

Handle<Object> ObjectRef::object() const { return data_->object(); }

#ifdef DEBUG
#define DEF_OBJECT_GETTER(T)                                                 \
  Handle<T> T##Ref::object() const {                                         \
    return Handle<T>(reinterpret_cast<Address*>(data_->object().address())); \
  }
#else
#define DEF_OBJECT_GETTER(T)                                                 \
  Handle<T> T##Ref::object() const {                                         \
    return Handle<T>(reinterpret_cast<Address*>(data_->object().address())); \
  }
#endif  // DEBUG

HEAP_BROKER_OBJECT_LIST(DEF_OBJECT_GETTER)
#undef DEF_OBJECT_GETTER

ObjectData* ObjectRef::data() const {
#ifdef DEBUG
  switch (JSHeapBroker::Current()->mode()) {
    case JSHeapBroker::kDisabled:
      break;
    case JSHeapBroker::kSerializing:
      CHECK_NE(data_->kind(), kUnserializedHeapObject);
      break;
    case JSHeapBroker::kSerialized:
    case JSHeapBroker::kRetired:
      CHECK_NE(data_->kind(), kUnserializedHeapObject);
      break;
  }
#endif

  return data_;
}

#define JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP(Result, Name, UsedField) \
  Result##Ref JSFunctionRef::Name(JSHeapBroker* broker) const {       \
    IF_ACCESS_FROM_HEAP(Result, Name);                                \
    RecordConsistentJSFunctionViewDependencyIfNeeded(                 \
        broker, *this, data()->AsJSFunction(), UsedField);            \
    return Result##Ref(data()->AsJSFunction()->Name());               \
  }

#define JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP_C(Result, Name, UsedField) \
  Result JSFunctionRef::Name(JSHeapBroker* broker) const {              \
    IF_ACCESS_FROM_HEAP_C(Name);                                        \
    RecordConsistentJSFunctionViewDependencyIfNeeded(                   \
        broker, *this, data()->AsJSFunction(), UsedField);              \
    return data()->AsJSFunction()->Name();                              \
  }

// Like JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP_C but only depend on the
// field in question if its recorded value is "relevant". This is in order to
// tolerate certain state changes during compilation, e.g. from "has no feedback
// vector" (in which case we would simply do less optimization) to "has feedback
// vector".
#define JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP_RELEVANT_C(     \
    Result, Name, UsedField, RelevantValue)                  \
  Result JSFunctionRef::Name(JSHeapBroker* broker) const {   \
    IF_ACCESS_FROM_HEAP_C(Name);                             \
    Result const result = data()->AsJSFunction()->Name();    \
    if (result == RelevantValue) {                           \
      RecordConsistentJSFunctionViewDependencyIfNeeded(      \
          broker, *this, data()->AsJSFunction(), UsedField); \
    }                                                        \
    return result;                                           \
  }

JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP_RELEVANT_C(bool, has_initial_map,
                                                JSFunctionData::kHasInitialMap,
                                                true)
JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP_RELEVANT_C(
    bool, has_instance_prototype, JSFunctionData::kHasInstancePrototype, true)
JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP_RELEVANT_C(
    bool, PrototypeRequiresRuntimeLookup,
    JSFunctionData::kPrototypeRequiresRuntimeLookup, false)

JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP(Map, initial_map,
                                     JSFunctionData::kInitialMap)
JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP(HeapObject, instance_prototype,
                                     JSFunctionData::kInstancePrototype)
JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP(FeedbackCell, raw_feedback_cell,
                                     JSFunctionData::kFeedbackCell)

BIMODAL_ACCESSOR(JSFunction, Context, context)
BIMODAL_ACCESSOR(JSFunction, SharedFunctionInfo, shared)

#undef JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP
#undef JSFUNCTION_BIMODAL_ACCESSOR_WITH_DEP_C

OptionalCodeRef JSFunctionRef::code(JSHeapBroker* broker) const {
  return TryMakeRef(broker, object()->code(broker->isolate()));
}

NativeContextRef JSFunctionRef::native_context(JSHeapBroker* broker) const {
  return MakeRefAssumeMemoryFence(broker,
                                  context(broker).object()->native_context());
}

OptionalFunctionTemplateInfoRef SharedFunctionInfoRef::function_template_info(
    JSHeapBroker* broker) const {
  if (!object()->IsApiFunction()) return {};
  return TryMakeRef(broker, object()->api_func_data());
}

int SharedFunctionInfoRef::context_header_size() const {
  return object()->scope_info()->ContextHeaderLength();
}

int SharedFunctionInfoRef::context_parameters_start() const {
  return object()->scope_info()->ParametersStartIndex();
}

ScopeInfoRef SharedFunctionInfoRef::scope_info(JSHeapBroker* broker) const {
  return MakeRefAssumeMemoryFence(broker, object()->scope_info(kAcquireLoad));
}

OptionalMapRef JSObjectRef::GetObjectCreateMap(JSHeapBroker* broker) const {
  DirectHandle<Map> map_handle = Cast<Map>(map(broker).object());
  // Note: implemented as an acquire-load.
  if (!map_handle->is_prototype_map()) return {};

  Handle<Object> maybe_proto_info = broker->CanonicalPersistentHandle(
      map_handle->prototype_info(kAcquireLoad));
  if (!IsPrototypeInfo(*maybe_proto_info)) return {};

  Tagged<MaybeObject> maybe_object_create_map =
      Cast<PrototypeInfo>(maybe_proto_info)->ObjectCreateMap(kAcquireLoad);
  if (!maybe_object_create_map.IsWeak()) return {};

  return MapRef(broker->GetOrCreateData(
      maybe_object_create_map.GetHeapObjectAssumeWeak(), kAssumeMemoryFence));
}

bool PropertyCellRef::Cache(JSHeapBroker* broker) const {
  if (data_->should_access_heap()) return true;
  CHECK(broker->mode() == JSHeapBroker::kSerializing ||
        broker->mode() == JSHeapBroker::kSerialized);
  return data()->AsPropertyCell()->Cache(broker);
}

bool NativeContextRef::GlobalIsDetached(JSHeapBroker* broker) const {
  ObjectRef proxy_proto =
      global_proxy_object(broker).map(broker).prototype(broker);
  return !proxy_proto.equals(global_object(broker));
}

OptionalPropertyCellRef JSGlobalObjectRef::GetPropertyCell(JSHeapBroker* broker,
                                                           NameRef name) const {
  std::optional<Tagged<PropertyCell>> maybe_cell =
      ConcurrentLookupIterator::TryGetPropertyCell(
          broker->isolate(), broker->local_isolate_or_isolate(),
          broker->target_native_context().global_object(broker).object(),
          name.object());
  if (!maybe_cell.has_value()) return {};
  return TryMakeRef(broker, *maybe_cell);
}

std::ostream& operator<<(std::ostream& os, ObjectRef ref) {
  if (!v8_flags.concurrent_recompilation) {
    // We cannot be in a background thread so it's safe to read the heap.
    AllowHandleDereference allow_handle_dereference;
    return os << ref.data() << " {" << ref.object() << "}";
  } else if (ref.data_->should_access_heap()) {
    return os << ref.data() << " {" << ref.object() << "}";
  } else {
    return os << ref.data();
  }
}

unsigned CodeRef::GetInlinedBytecodeSize() const {
  Tagged<Code> code = *object();
  const unsigned value = code->inlined_bytecode_size();
  if (value != 0 && code->marked_for_deoptimization()) {
    // Don't report inlined bytecode size if the code object was already
    // deoptimized.
    return 0;
  }
  return value;
}

#undef BIMODAL_ACCESSOR
#undef BIMODAL_ACCESSOR_B
#undef BIMODAL_ACCESSOR_C
#undef HEAP_ACCESSOR_B
#undef HEAP_ACCESSOR_C
#undef IF_ACCESS_FROM_HEAP
#undef IF_ACCESS_FROM_HEAP_C
#undef TRACE
#undef TRACE_MISSING

}  // namespace compiler
}  // namespace internal
}  // namespace v8

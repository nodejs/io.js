// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_HEAP_BROKER_H_
#define V8_COMPILER_JS_HEAP_BROKER_H_

#include "src/base/compiler-specific.h"
#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/base/platform/mutex.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/compiler/access-info.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/processed-feedback.h"
#include "src/compiler/refs-map.h"
#include "src/execution/local-isolate.h"
#include "src/handles/handles.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/objects/code-kind.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/objects.h"
#include "src/objects/tagged.h"
#include "src/roots/roots.h"
#include "src/utils/address-map.h"
#include "src/utils/identity-map.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

namespace maglev {
class MaglevCompilationInfo;
}

namespace compiler {

class ObjectRef;

std::ostream& operator<<(std::ostream& os, ObjectRef ref);

#define TRACE_BROKER(broker, x)                                          \
  do {                                                                   \
    if (broker->tracing_enabled() && v8_flags.trace_heap_broker_verbose) \
      StdoutStream{} << broker->Trace() << x << '\n';                    \
  } while (false)

#define TRACE_BROKER_MEMORY(broker, x)                                  \
  do {                                                                  \
    if (broker->tracing_enabled() && v8_flags.trace_heap_broker_memory) \
      StdoutStream{} << broker->Trace() << x << std::endl;              \
  } while (false)

#define TRACE_BROKER_MISSING(broker, x)                                        \
  do {                                                                         \
    if (broker->tracing_enabled())                                             \
      StdoutStream{} << broker->Trace() << "Missing " << x << " (" << __FILE__ \
                     << ":" << __LINE__ << ")" << std::endl;                   \
  } while (false)

struct PropertyAccessTarget {
  MapRef map;
  NameRef name;
  AccessMode mode;

  struct Hash {
    size_t operator()(const PropertyAccessTarget& pair) const {
      return base::hash_combine(
          base::hash_combine(pair.map.object().address(),
                             pair.name.object().address()),
          static_cast<int>(pair.mode));
    }
  };
  struct Equal {
    bool operator()(const PropertyAccessTarget& lhs,
                    const PropertyAccessTarget& rhs) const {
      return lhs.map.equals(rhs.map) && lhs.name.equals(rhs.name) &&
             lhs.mode == rhs.mode;
    }
  };
};

enum GetOrCreateDataFlag {
  // If set, a failure to create the data object results in a crash.
  kCrashOnError = 1 << 0,
  // If set, data construction assumes that the given object is protected by
  // a memory fence (e.g. acquire-release) and thus fields required for
  // construction (like Object::map) are safe to read. The protection can
  // extend to some other situations as well.
  kAssumeMemoryFence = 1 << 1,
};
using GetOrCreateDataFlags = base::Flags<GetOrCreateDataFlag>;
DEFINE_OPERATORS_FOR_FLAGS(GetOrCreateDataFlags)

class V8_EXPORT_PRIVATE JSHeapBroker {
 public:
  JSHeapBroker(Isolate* isolate, Zone* broker_zone, bool tracing_enabled,
               CodeKind code_kind);

  // For use only in tests, sets default values for some arguments. Avoids
  // churn when new flags are added.
  JSHeapBroker(Isolate* isolate, Zone* broker_zone)
      : JSHeapBroker(isolate, broker_zone, v8_flags.trace_heap_broker,
                     CodeKind::TURBOFAN) {}

  ~JSHeapBroker();

  // The compilation target's native context. We need the setter because at
  // broker construction time we don't yet have the canonical handle.
  NativeContextRef target_native_context() const {
    return target_native_context_.value();
  }
  void SetTargetNativeContextRef(Handle<NativeContext> native_context);

  void InitializeAndStartSerializing(Handle<NativeContext> native_context);

  Isolate* isolate() const { return isolate_; }

  // The pointer compression cage base value used for decompression of all
  // tagged values except references to InstructionStream objects.
  PtrComprCageBase cage_base() const {
#if V8_COMPRESS_POINTERS
    return cage_base_;
#else
    return PtrComprCageBase{};
#endif  // V8_COMPRESS_POINTERS
  }

  Zone* zone() const { return zone_; }
  bool tracing_enabled() const { return tracing_enabled_; }

  NexusConfig feedback_nexus_config() const {
    return IsMainThread() ? NexusConfig::FromMainThread(isolate())
                          : NexusConfig::FromBackgroundThread(
                                isolate(), local_isolate()->heap());
  }

  enum BrokerMode { kDisabled, kSerializing, kSerialized, kRetired };
  BrokerMode mode() const { return mode_; }

  void StopSerializing();
  void Retire();
  bool SerializingAllowed() const;

#ifdef DEBUG
  // Get the current heap broker for this thread. Only to be used for DCHECKs.
  static JSHeapBroker* Current();
#endif

  // Remember the local isolate and initialize its local heap with the
  // persistent and canonical handles provided by {info}.
  void AttachLocalIsolate(OptimizedCompilationInfo* info,
                          LocalIsolate* local_isolate);
  // Forget about the local isolate and pass the persistent and canonical
  // handles provided back to {info}. {info} is responsible for disposing of
  // them.
  void DetachLocalIsolate(OptimizedCompilationInfo* info);

  // TODO(v8:7700): Refactor this once the broker is no longer
  // Turbofan-specific.
  void AttachLocalIsolateForMaglev(maglev::MaglevCompilationInfo* info,
                                   LocalIsolate* local_isolate);
  void DetachLocalIsolateForMaglev(maglev::MaglevCompilationInfo* info);

  // Attaches the canonical handles map from the compilation info to the broker.
  // Ownership of the map remains in the compilation info.
  template <typename CompilationInfoT>
  void AttachCompilationInfo(CompilationInfoT* info) {
    set_canonical_handles(info->canonical_handles());
  }

  bool StackHasOverflowed() const;

#ifdef DEBUG
  void PrintRefsAnalysis() const;
#endif  // DEBUG

  // Returns the handle from root index table for read only heap objects.
  Handle<Object> GetRootHandle(Tagged<Object> object);

  // Never returns nullptr.
  ObjectData* GetOrCreateData(Handle<Object> object,
                              GetOrCreateDataFlags flags = {});
  ObjectData* GetOrCreateData(Tagged<Object> object,
                              GetOrCreateDataFlags flags = {});

  // Gets data only if we have it. However, thin wrappers will be created for
  // smis, read-only objects and never-serialized objects.
  ObjectData* TryGetOrCreateData(Handle<Object> object,
                                 GetOrCreateDataFlags flags = {});
  ObjectData* TryGetOrCreateData(Tagged<Object> object,
                                 GetOrCreateDataFlags flags = {});

  // Check if {object} is any native context's %ArrayPrototype% or
  // %ObjectPrototype%.
  bool IsArrayOrObjectPrototype(JSObjectRef object) const;
  bool IsArrayOrObjectPrototype(Handle<JSObject> object) const;

  bool HasFeedback(FeedbackSource const& source) const;
  void SetFeedback(FeedbackSource const& source,
                   ProcessedFeedback const* feedback);
  FeedbackSlotKind GetFeedbackSlotKind(FeedbackSource const& source) const;

  ElementAccessFeedback const& ProcessFeedbackMapsForElementAccess(
      ZoneVector<MapRef>& maps, KeyedAccessMode const& keyed_mode,
      FeedbackSlotKind slot_kind);

  // Binary, comparison and for-in hints can be fully expressed via
  // an enum. Insufficient feedback is signaled by <Hint enum>::kNone.
  BinaryOperationHint GetFeedbackForBinaryOperation(
      FeedbackSource const& source);
  CompareOperationHint GetFeedbackForCompareOperation(
      FeedbackSource const& source);
  ForInHint GetFeedbackForForIn(FeedbackSource const& source);

  ProcessedFeedback const& GetFeedbackForCall(FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForGlobalAccess(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForInstanceOf(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForArrayOrObjectLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForRegExpLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForTemplateObject(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForPropertyAccess(
      FeedbackSource const& source, AccessMode mode,
      OptionalNameRef static_name);

  ProcessedFeedback const& ProcessFeedbackForBinaryOperation(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForCompareOperation(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForForIn(
      FeedbackSource const& source);

  bool FeedbackIsInsufficient(FeedbackSource const& source) const;

  OptionalNameRef GetNameFeedback(FeedbackNexus const& nexus);

  PropertyAccessInfo GetPropertyAccessInfo(MapRef map, NameRef name,
                                           AccessMode access_mode);

  StringRef GetTypedArrayStringTag(ElementsKind kind);

  bool IsMainThread() const {
    return local_isolate() == nullptr || local_isolate()->is_main_thread();
  }

  LocalIsolate* local_isolate() const { return local_isolate_; }

  // TODO(jgruber): Consider always having local_isolate_ set to a real value.
  // This seems not entirely trivial since we currently reset local_isolate_ to
  // nullptr at some point in the JSHeapBroker lifecycle.
  LocalIsolate* local_isolate_or_isolate() const {
    return local_isolate() != nullptr ? local_isolate()
                                      : isolate()->AsLocalIsolate();
  }

  base::Optional<RootIndex> FindRootIndex(HeapObjectRef object) {
    // No root constant is a JSReceiver.
    if (object.IsJSReceiver()) return {};
    Address address = object.object()->ptr();
    RootIndex root_index;
    if (root_index_map_.Lookup(address, &root_index)) {
      return root_index;
    }
    return {};
  }

  // Return the corresponding canonical persistent handle for {object}. Create
  // one if it does not exist.
  // If a local isolate is attached, we can create the persistent handle through
  // it. This commonly happens during the Execute phase.
  // If we don't, that means we are calling this method from serialization. If
  // that happens, we should be inside a persistent handle scope. Then, we would
  // just use the regular handle creation.
  template <typename T>
  Handle<T> CanonicalPersistentHandle(Tagged<T> object) {
    DCHECK_NOT_NULL(canonical_handles_);
    Address address = object.ptr();
    if (Internals::HasHeapObjectTag(address)) {
      RootIndex root_index;
      // CollectArrayAndObjectPrototypes calls this function often with T equal
      // to JSObject. The root index map only contains immortal, immutable
      // objects; it never contains any instances of type JSObject, since
      // JSObjects must exist within a NativeContext, and NativeContexts can be
      // created and destroyed. Thus, we can skip the lookup in the root index
      // map for those values and save a little time.
      if constexpr (std::is_convertible_v<T, JSObject>) {
        DCHECK(!root_index_map_.Lookup(address, &root_index));
      } else if (root_index_map_.Lookup(address, &root_index)) {
        return Handle<T>(isolate_->root_handle(root_index).location());
      }
    }

    Tagged<Object> obj(address);
    auto find_result = canonical_handles_->FindOrInsert(obj);
    if (find_result.already_exists) return Handle<T>(*find_result.entry);

    // Allocate new PersistentHandle if one wasn't created before.
    if (local_isolate()) {
      *find_result.entry =
          local_isolate()->heap()->NewPersistentHandle(obj).location();
    } else {
      DCHECK(PersistentHandlesScope::IsActive(isolate()));
      *find_result.entry = Handle<T>(object, isolate()).location();
    }
    return Handle<T>(*find_result.entry);
  }

  template <typename T>
  Handle<T> CanonicalPersistentHandle(Handle<T> object) {
    if (object.is_null()) return object;  // Can't deref a null handle.
    return CanonicalPersistentHandle<T>(*object);
  }

  // Checks if a canonical persistent handle for {object} exists.
  template <typename T>
  bool IsCanonicalHandle(Handle<T> handle) {
    DCHECK_NOT_NULL(canonical_handles_);
    Address* location = handle.location();
    Address address = *location;
    if (Internals::HasHeapObjectTag(address)) {
      RootIndex root_index;
      if (root_index_map_.Lookup(address, &root_index)) {
        return true;
      }
      // Builtins use pseudo handles that are canonical and persistent by
      // design.
      if (isolate()->IsBuiltinTableHandleLocation(location)) {
        return true;
      }
    }
    return canonical_handles_->Find(Tagged<Object>(address)) != nullptr;
  }

  std::string Trace() const;
  void IncrementTracingIndentation();
  void DecrementTracingIndentation();

  // Locks {mutex} through the duration of this scope iff it is the first
  // occurrence. This is done to have a recursive shared lock on {mutex}.
  class V8_NODISCARD RecursiveSharedMutexGuardIfNeeded {
   protected:
    V8_INLINE RecursiveSharedMutexGuardIfNeeded(LocalIsolate* local_isolate,
                                                base::SharedMutex* mutex,
                                                int* mutex_depth_address);

    ~RecursiveSharedMutexGuardIfNeeded() {
      DCHECK_GE((*mutex_depth_address_), 1);
      (*mutex_depth_address_)--;
      DCHECK_EQ(initial_mutex_depth_, (*mutex_depth_address_));
    }

   private:
    int* const mutex_depth_address_;
    const int initial_mutex_depth_;
    ParkedSharedMutexGuardIf<base::kShared> shared_mutex_guard_;
  };

  class MapUpdaterGuardIfNeeded final
      : public RecursiveSharedMutexGuardIfNeeded {
   public:
    V8_INLINE explicit MapUpdaterGuardIfNeeded(JSHeapBroker* broker);
  };

  class BoilerplateMigrationGuardIfNeeded final
      : public RecursiveSharedMutexGuardIfNeeded {
   public:
    V8_INLINE explicit BoilerplateMigrationGuardIfNeeded(JSHeapBroker* broker);
  };

  // If this returns false, the object is guaranteed to be fully initialized and
  // thus safe to read from a memory safety perspective. The converse does not
  // necessarily hold.
  bool ObjectMayBeUninitialized(Handle<Object> object) const;
  bool ObjectMayBeUninitialized(Tagged<Object> object) const;
  bool ObjectMayBeUninitialized(Tagged<HeapObject> object) const;

  void set_dependencies(CompilationDependencies* dependencies) {
    DCHECK_NOT_NULL(dependencies);
    DCHECK_NULL(dependencies_);
    dependencies_ = dependencies;
  }
  CompilationDependencies* dependencies() const {
    DCHECK_NOT_NULL(dependencies_);
    return dependencies_;
  }

#define V(Type, name, Name) inline typename ref_traits<Type>::ref_type name();
  READ_ONLY_ROOT_LIST(V)
#undef V

 private:
  friend class JSHeapBrokerScopeForTesting;
  friend class HeapObjectRef;
  friend class ObjectRef;
  friend class ObjectData;
  friend class PropertyCellData;

  ProcessedFeedback const& GetFeedback(FeedbackSource const& source) const;
  const ProcessedFeedback& NewInsufficientFeedback(FeedbackSlotKind kind) const;

  // Bottleneck FeedbackNexus access here, for storage in the broker
  // or on-the-fly usage elsewhere in the compiler.
  ProcessedFeedback const& ReadFeedbackForArrayOrObjectLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForBinaryOperation(
      FeedbackSource const& source) const;
  ProcessedFeedback const& ReadFeedbackForCall(FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForCompareOperation(
      FeedbackSource const& source) const;
  ProcessedFeedback const& ReadFeedbackForForIn(
      FeedbackSource const& source) const;
  ProcessedFeedback const& ReadFeedbackForGlobalAccess(
      JSHeapBroker* broker, FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForInstanceOf(
      FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForPropertyAccess(
      FeedbackSource const& source, AccessMode mode,
      OptionalNameRef static_name);
  ProcessedFeedback const& ReadFeedbackForRegExpLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForTemplateObject(
      FeedbackSource const& source);

  void CollectArrayAndObjectPrototypes();

  void set_persistent_handles(
      std::unique_ptr<PersistentHandles> persistent_handles) {
    DCHECK_NULL(ph_);
    ph_ = std::move(persistent_handles);
    DCHECK_NOT_NULL(ph_);
  }
  std::unique_ptr<PersistentHandles> DetachPersistentHandles() {
    DCHECK_NOT_NULL(ph_);
    return std::move(ph_);
  }

  void set_canonical_handles(CanonicalHandlesMap* canonical_handles) {
    canonical_handles_ = canonical_handles;
  }

#define V(Type, name, Name) void Init##Name();
  READ_ONLY_ROOT_LIST(V)
#undef V

  Isolate* const isolate_;
#if V8_COMPRESS_POINTERS
  const PtrComprCageBase cage_base_;
#endif  // V8_COMPRESS_POINTERS
  Zone* const zone_;
  OptionalNativeContextRef target_native_context_;
  RefsMap* refs_;
  RootIndexMap root_index_map_;
  ZoneUnorderedSet<Handle<JSObject>, Handle<JSObject>::hash,
                   Handle<JSObject>::equal_to>
      array_and_object_prototypes_;
  BrokerMode mode_ = kDisabled;
  bool const tracing_enabled_;
  CodeKind const code_kind_;
  std::unique_ptr<PersistentHandles> ph_;
  LocalIsolate* local_isolate_ = nullptr;
  // The CanonicalHandlesMap is owned by the compilation info.
  CanonicalHandlesMap* canonical_handles_;
  unsigned trace_indentation_ = 0;
  ZoneUnorderedMap<FeedbackSource, ProcessedFeedback const*,
                   FeedbackSource::Hash, FeedbackSource::Equal>
      feedback_;
  ZoneUnorderedMap<PropertyAccessTarget, PropertyAccessInfo,
                   PropertyAccessTarget::Hash, PropertyAccessTarget::Equal>
      property_access_infos_;

  // Cache read only roots to avoid needing to look them up via the map.
#define V(Type, name, Name) \
  OptionalRef<typename ref_traits<Type>::ref_type> name##_;
  READ_ONLY_ROOT_LIST(V)
#undef V

  CompilationDependencies* dependencies_ = nullptr;

  // The MapUpdater mutex is used in recursive patterns; for example,
  // ComputePropertyAccessInfo may call itself recursively. Thus we need to
  // emulate a recursive mutex, which we do by checking if this heap broker
  // instance already holds the mutex when a lock is requested. This field
  // holds the locking depth, i.e. how many times the mutex has been
  // recursively locked. Only the outermost locker actually locks underneath.
  int map_updater_mutex_depth_ = 0;
  // Likewise for boilerplate migrations.
  int boilerplate_migration_mutex_depth_ = 0;

  static constexpr uint32_t kMinimalRefsBucketCount = 8;
  static_assert(base::bits::IsPowerOfTwo(kMinimalRefsBucketCount));
  static constexpr uint32_t kInitialRefsBucketCount = 1024;
  static_assert(base::bits::IsPowerOfTwo(kInitialRefsBucketCount));
};

#ifdef DEBUG
// In debug builds, store the current heap broker on a thread local, for
// DCHECKs to access it via JSHeapBroker::Current();
class V8_NODISCARD V8_EXPORT_PRIVATE CurrentHeapBrokerScope {
 public:
  explicit CurrentHeapBrokerScope(JSHeapBroker* broker);
  ~CurrentHeapBrokerScope();

 private:
  JSHeapBroker* const prev_broker_;
};
#else
class V8_NODISCARD V8_EXPORT_PRIVATE CurrentHeapBrokerScope {
 public:
  explicit CurrentHeapBrokerScope(JSHeapBroker* broker) {}
  ~CurrentHeapBrokerScope() {}
};
#endif

class V8_NODISCARD TraceScope {
 public:
  TraceScope(JSHeapBroker* broker, const char* label)
      : TraceScope(broker, static_cast<void*>(broker), label) {}

  TraceScope(JSHeapBroker* broker, ObjectData* data, const char* label)
      : TraceScope(broker, static_cast<void*>(data), label) {}

  TraceScope(JSHeapBroker* broker, void* subject, const char* label)
      : broker_(broker) {
    TRACE_BROKER(broker_, "Running " << label << " on " << subject);
    broker_->IncrementTracingIndentation();
  }

  ~TraceScope() { broker_->DecrementTracingIndentation(); }

 private:
  JSHeapBroker* const broker_;
};

// Scope that unparks the LocalHeap, if:
//   a) We have a JSHeapBroker,
//   b) Said JSHeapBroker has a LocalIsolate and thus a LocalHeap,
//   c) Said LocalHeap has been parked and
//   d) The given condition evaluates to true.
// Used, for example, when printing the graph with --trace-turbo with a
// previously parked LocalHeap.
class V8_NODISCARD UnparkedScopeIfNeeded {
 public:
  explicit UnparkedScopeIfNeeded(JSHeapBroker* broker,
                                 bool extra_condition = true) {
    if (broker != nullptr && extra_condition) {
      LocalIsolate* local_isolate = broker->local_isolate();
      if (local_isolate != nullptr && local_isolate->heap()->IsParked()) {
        unparked_scope.emplace(local_isolate->heap());
      }
    }
  }

 private:
  base::Optional<UnparkedScope> unparked_scope;
};

class V8_NODISCARD JSHeapBrokerScopeForTesting {
 public:
  JSHeapBrokerScopeForTesting(JSHeapBroker* broker, Isolate* isolate,
                              Zone* zone)
      : JSHeapBrokerScopeForTesting(
            broker, std::make_unique<CanonicalHandlesMap>(
                        isolate->heap(), ZoneAllocationPolicy(zone))) {}
  JSHeapBrokerScopeForTesting(
      JSHeapBroker* broker,
      std::unique_ptr<CanonicalHandlesMap> canonical_handles)
      : canonical_handles_(std::move(canonical_handles)), broker_(broker) {
    broker_->set_canonical_handles(canonical_handles_.get());
  }
  ~JSHeapBrokerScopeForTesting() { broker_->set_canonical_handles(nullptr); }

 private:
  std::unique_ptr<CanonicalHandlesMap> canonical_handles_;
  JSHeapBroker* const broker_;
};

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
OptionalRef<typename ref_traits<T>::ref_type> TryMakeRef(JSHeapBroker* broker,
                                                         ObjectData* data) {
  if (data == nullptr) return {};
  return {typename ref_traits<T>::ref_type(data)};
}

// Usage:
//
//  OptionalFooRef ref = TryMakeRef(broker, o);
//  if (!ref.has_value()) return {};  // bailout
//
// or
//
//  FooRef ref = MakeRef(broker, o);
template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
OptionalRef<typename ref_traits<T>::ref_type> TryMakeRef(
    JSHeapBroker* broker, Tagged<T> object, GetOrCreateDataFlags flags = {}) {
  ObjectData* data = broker->TryGetOrCreateData(object, flags);
  if (data == nullptr) {
    TRACE_BROKER_MISSING(broker, "ObjectData for " << Brief(object));
  }
  return TryMakeRef<T>(broker, data);
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
OptionalRef<typename ref_traits<T>::ref_type> TryMakeRef(
    JSHeapBroker* broker, T object, GetOrCreateDataFlags flags = {}) {
  static_assert(kTaggedCanConvertToRawObjects);
  return TryMakeRef<T>(broker, Tagged<T>(object), flags);
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
OptionalRef<typename ref_traits<T>::ref_type> TryMakeRef(
    JSHeapBroker* broker, Handle<T> object, GetOrCreateDataFlags flags = {}) {
  ObjectData* data = broker->TryGetOrCreateData(object, flags);
  if (data == nullptr) {
    DCHECK_EQ(flags & kCrashOnError, 0);
    TRACE_BROKER_MISSING(broker, "ObjectData for " << Brief(*object));
  }
  return TryMakeRef<T>(broker, data);
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
typename ref_traits<T>::ref_type MakeRef(JSHeapBroker* broker,
                                         Tagged<T> object) {
  return TryMakeRef(broker, object, kCrashOnError).value();
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
typename ref_traits<T>::ref_type MakeRef(JSHeapBroker* broker, T object) {
  static_assert(kTaggedCanConvertToRawObjects);
  return TryMakeRef(broker, Tagged<T>(object), kCrashOnError).value();
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
typename ref_traits<T>::ref_type MakeRef(JSHeapBroker* broker,
                                         Handle<T> object) {
  return TryMakeRef(broker, object, kCrashOnError).value();
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
typename ref_traits<T>::ref_type MakeRefAssumeMemoryFence(JSHeapBroker* broker,
                                                          Tagged<T> object) {
  return TryMakeRef(broker, object, kAssumeMemoryFence | kCrashOnError).value();
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
typename ref_traits<T>::ref_type MakeRefAssumeMemoryFence(JSHeapBroker* broker,
                                                          T object) {
  static_assert(kTaggedCanConvertToRawObjects);
  return TryMakeRef(broker, object, kAssumeMemoryFence | kCrashOnError).value();
}

template <class T, typename = std::enable_if_t<is_subtype_v<T, Object>>>
typename ref_traits<T>::ref_type MakeRefAssumeMemoryFence(JSHeapBroker* broker,
                                                          Handle<T> object) {
  return TryMakeRef(broker, object, kAssumeMemoryFence | kCrashOnError).value();
}

#define V(Type, name, Name)                                         \
  inline typename ref_traits<Type>::ref_type JSHeapBroker::name() { \
    if (!name##_) {                                                 \
      Init##Name();                                                 \
    }                                                               \
    return name##_.value();                                         \
  }
READ_ONLY_ROOT_LIST(V)
#undef V

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_HEAP_BROKER_H_

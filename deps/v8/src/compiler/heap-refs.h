// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_HEAP_REFS_H_
#define V8_COMPILER_HEAP_REFS_H_

#include <optional>
#include <type_traits>

#include "src/ic/call-optimization.h"
#include "src/objects/elements-kind.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/instance-type.h"
#include "src/objects/object-list-macros.h"
#include "src/utils/boxed-float.h"
#include "src/zone/zone-compact-set.h"

namespace v8 {

class CFunctionInfo;

namespace internal {

class BytecodeArray;
class FixedDoubleArray;
class FunctionTemplateInfo;
class HeapNumber;
class InternalizedString;
class JSBoundFunction;
class JSDataView;
class JSGlobalProxy;
class JSTypedArray;
class NativeContext;
class ScriptContextTable;
template <typename>
class Signature;

namespace interpreter {
class Register;
}  // namespace interpreter

namespace wasm {
class ValueType;
struct WasmModule;
}  // namespace wasm

namespace compiler {

class CompilationDependencies;
struct FeedbackSource;
class JSHeapBroker;
class ObjectData;
class PerIsolateCompilerCache;
class PropertyAccessInfo;

// Whether we are loading a property or storing to a property.
// For a store during literal creation, do not walk up the prototype chain.
// For a define operation, we behave similarly to kStoreInLiteral, but with
// distinct semantics for private class fields (in which private field
// accesses must throw when storing a field which does not exist, or
// adding/defining a field which already exists).
enum class AccessMode { kLoad, kStore, kStoreInLiteral, kHas, kDefine };

inline bool IsAnyStore(AccessMode mode) {
  return mode == AccessMode::kStore || mode == AccessMode::kStoreInLiteral ||
         mode == AccessMode::kDefine;
}

inline bool IsDefiningStore(AccessMode mode) {
  return mode == AccessMode::kStoreInLiteral || mode == AccessMode::kDefine;
}

enum class OddballType : uint8_t {
  kNone,     // Not an Oddball.
  kBoolean,  // True or False.
  kUndefined,
  kNull,
};

enum class HoleType : uint8_t {
  kNone,  // Not a Hole.

#define FOR_HOLE(Name, name, Root) k##Name,
  HOLE_LIST(FOR_HOLE)
#undef FOR_HOLE

      kGeneric = kTheHole,
};

enum class RefSerializationKind {
  // Skips serialization.
  kNeverSerialized,
  // Can be serialized on demand from the background thread.
  kBackgroundSerialized,
};

// This list is sorted such that subtypes appear before their supertypes.
// DO NOT VIOLATE THIS PROPERTY!
#define HEAP_BROKER_OBJECT_LIST_BASE(BACKGROUND_SERIALIZED, NEVER_SERIALIZED) \
  /* Subtypes of JSObject */                                                  \
  BACKGROUND_SERIALIZED(JSArray)                                              \
  BACKGROUND_SERIALIZED(JSBoundFunction)                                      \
  BACKGROUND_SERIALIZED(JSDataView)                                           \
  BACKGROUND_SERIALIZED(JSFunction)                                           \
  BACKGROUND_SERIALIZED(JSGlobalObject)                                       \
  BACKGROUND_SERIALIZED(JSGlobalProxy)                                        \
  BACKGROUND_SERIALIZED(JSTypedArray)                                         \
  BACKGROUND_SERIALIZED(JSPrimitiveWrapper)                                   \
  /* Subtypes of Context */                                                   \
  NEVER_SERIALIZED(NativeContext)                                             \
  /* Subtypes of FixedArray */                                                \
  NEVER_SERIALIZED(ObjectBoilerplateDescription)                              \
  BACKGROUND_SERIALIZED(ScriptContextTable)                                   \
  /* Subtypes of String */                                                    \
  NEVER_SERIALIZED(InternalizedString)                                        \
  /* Subtypes of FixedArrayBase */                                            \
  BACKGROUND_SERIALIZED(FixedArray)                                           \
  NEVER_SERIALIZED(FixedDoubleArray)                                          \
  /* Subtypes of Name */                                                      \
  NEVER_SERIALIZED(String)                                                    \
  NEVER_SERIALIZED(Symbol)                                                    \
  /* Subtypes of JSReceiver */                                                \
  BACKGROUND_SERIALIZED(JSObject)                                             \
  /* Subtypes of HeapObject */                                                \
  NEVER_SERIALIZED(AccessorInfo)                                              \
  NEVER_SERIALIZED(AllocationSite)                                            \
  NEVER_SERIALIZED(ArrayBoilerplateDescription)                               \
  BACKGROUND_SERIALIZED(BigInt)                                               \
  NEVER_SERIALIZED(BytecodeArray)                                             \
  NEVER_SERIALIZED(Cell)                                                      \
  NEVER_SERIALIZED(Code)                                                      \
  NEVER_SERIALIZED(Context)                                                   \
  NEVER_SERIALIZED(DescriptorArray)                                           \
  NEVER_SERIALIZED(FeedbackCell)                                              \
  NEVER_SERIALIZED(FeedbackVector)                                            \
  BACKGROUND_SERIALIZED(FixedArrayBase)                                       \
  NEVER_SERIALIZED(FunctionTemplateInfo)                                      \
  NEVER_SERIALIZED(HeapNumber)                                                \
  BACKGROUND_SERIALIZED(JSReceiver)                                           \
  BACKGROUND_SERIALIZED(Map)                                                  \
  NEVER_SERIALIZED(Name)                                                      \
  BACKGROUND_SERIALIZED(PropertyCell)                                         \
  NEVER_SERIALIZED(RegExpBoilerplateDescription)                              \
  NEVER_SERIALIZED(ScopeInfo)                                                 \
  NEVER_SERIALIZED(SharedFunctionInfo)                                        \
  NEVER_SERIALIZED(SourceTextModule)                                          \
  NEVER_SERIALIZED(TemplateObjectDescription)                                 \
  /* Subtypes of Object */                                                    \
  BACKGROUND_SERIALIZED(HeapObject)

#define HEAP_BROKER_OBJECT_LIST(V) HEAP_BROKER_OBJECT_LIST_BASE(V, V)
#define IGNORE_CASE(...)
#define HEAP_BROKER_BACKGROUND_SERIALIZED_OBJECT_LIST(V) \
  HEAP_BROKER_OBJECT_LIST_BASE(V, IGNORE_CASE)

#define FORWARD_DECL(Name) class Name##Ref;
HEAP_BROKER_OBJECT_LIST(FORWARD_DECL)
FORWARD_DECL(Object)
#undef FORWARD_DECL

template <class T>
struct is_ref : public std::false_type {};

#define DEFINE_IS_REF(Name) \
  template <>               \
  struct is_ref<Name##Ref> : public std::true_type {};
HEAP_BROKER_OBJECT_LIST(DEFINE_IS_REF)
DEFINE_IS_REF(Object)
#undef DEFINE_IS_REF

template <class T>
struct ref_traits;

#define FORWARD_DECL(Name) class Name##Data;
HEAP_BROKER_BACKGROUND_SERIALIZED_OBJECT_LIST(FORWARD_DECL)
#undef FORWARD_DECL

#define BACKGROUND_SERIALIZED_REF_TRAITS(Name)                     \
  template <>                                                      \
  struct ref_traits<Name> {                                        \
    using ref_type = Name##Ref;                                    \
    using data_type = Name##Data;                                  \
    static constexpr RefSerializationKind ref_serialization_kind = \
        RefSerializationKind::kBackgroundSerialized;               \
  };

#define NEVER_SERIALIZED_REF_TRAITS(Name)                          \
  template <>                                                      \
  struct ref_traits<Name> {                                        \
    using ref_type = Name##Ref;                                    \
    using data_type = ObjectData;                                  \
    static constexpr RefSerializationKind ref_serialization_kind = \
        RefSerializationKind::kNeverSerialized;                    \
  };

HEAP_BROKER_OBJECT_LIST_BASE(BACKGROUND_SERIALIZED_REF_TRAITS,
                             NEVER_SERIALIZED_REF_TRAITS)
#undef NEVER_SERIALIZED_REF_TRAITS
#undef BACKGROUND_SERIALIZED_REF_TRAITS

template <>
struct ref_traits<Object> {
  using ref_type = ObjectRef;
  using data_type = ObjectData;
  // Note: While a bit awkward, this artificial ref serialization kind value is
  // okay: smis are never-serialized, and we never create raw non-smi
  // ObjectRefs (they would at least be HeapObjectRefs instead).
  static constexpr RefSerializationKind ref_serialization_kind =
      RefSerializationKind::kNeverSerialized;
};

// For types which don't have a corresponding Ref type, use the next best
// existing Ref.
template <>
struct ref_traits<Oddball> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<Null> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<Undefined> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<True> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<False> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<Hole> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<EnumCache> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<PropertyArray> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<ByteArray> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<ExternalPointerArray> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<TrustedFixedArray> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<ClosureFeedbackCellArray> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<NumberDictionary> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<OrderedHashMap> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<OrderedHashSet> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<FeedbackMetadata> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<NameDictionary> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<OrderedNameDictionary> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<SwissNameDictionary> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<InterceptorInfo> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<ArrayList> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<WeakFixedArray> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<WeakArrayList> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<RegisteredSymbolTable> : public ref_traits<HeapObject> {};
#if V8_ENABLE_WEBASSEMBLY
template <>
struct ref_traits<WasmNull> : public ref_traits<HeapObject> {};
#endif  // V8_ENABLE_WEBASSEMBLY
template <>
struct ref_traits<Smi> : public ref_traits<Object> {};
template <>
struct ref_traits<Boolean> : public ref_traits<HeapObject> {};
template <>
struct ref_traits<JSProxy> : public ref_traits<JSReceiver> {};
template <>
struct ref_traits<JSWrappedFunction> : public ref_traits<JSFunction> {};

template <class... T>
struct ref_traits<Union<T...>> {
  // There's no good way in C++ to find a common base class, so just test a few
  // common cases.
  static constexpr bool kAllJSReceiverRef =
      (std::is_base_of_v<JSReceiverRef, typename ref_traits<T>::ref_type> &&
       ...);
  static constexpr bool kAllHeapObjectRef =
      (std::is_base_of_v<JSReceiverRef, typename ref_traits<T>::ref_type> &&
       ...);

  using ref_type = std::conditional_t<
      kAllJSReceiverRef, JSReceiverRef,
      std::conditional_t<kAllHeapObjectRef, HeapObjectRef, ObjectRef>>;
  using data_type = std::conditional_t<
      kAllJSReceiverRef, JSReceiverData,
      std::conditional_t<kAllHeapObjectRef, HeapObjectData, ObjectData>>;

  static constexpr RefSerializationKind ref_serialization_kind =
      ((ref_traits<T>::ref_serialization_kind ==
        RefSerializationKind::kNeverSerialized) &&
       ...)
          ? RefSerializationKind::kNeverSerialized
          : RefSerializationKind::kBackgroundSerialized;
};

// Wrapper around heap refs which works roughly like a std::optional, but
// doesn't use extra storage for a boolean, but instead uses a null data pointer
// as a sentinel no value.
template <typename TRef>
class OptionalRef {
 public:
  // {ArrowOperatorHelper} is returned by {OptionalRef::operator->}. It should
  // never be stored anywhere or used in any other code; no one should ever have
  // to spell out {ArrowOperatorHelper} in code. Its only purpose is to be
  // dereferenced immediately by "operator-> chaining". Returning the address of
  // the field is valid because this objects lifetime only ends at the end of
  // the full statement.
  class ArrowOperatorHelper {
   public:
    TRef* operator->() { return &object_; }

   private:
    friend class OptionalRef<TRef>;
    explicit ArrowOperatorHelper(TRef object) : object_(object) {}

    TRef object_;
  };

  OptionalRef() = default;
  // NOLINTNEXTLINE
  OptionalRef(std::nullopt_t) : OptionalRef() {}

  // Allow implicit upcasting from OptionalRefs with compatible refs.
  template <typename SRef, typename = typename std::enable_if<
                               std::is_convertible<SRef*, TRef*>::value>::type>
  // NOLINTNEXTLINE
  V8_INLINE OptionalRef(OptionalRef<SRef> ref) : data_(ref.data_) {}

  // Allow implicit upcasting from compatible refs.
  template <typename SRef, typename = typename std::enable_if<
                               std::is_convertible<SRef*, TRef*>::value>::type>
  // NOLINTNEXTLINE
  V8_INLINE OptionalRef(SRef ref) : data_(ref.data_) {}

  constexpr bool has_value() const { return data_ != nullptr; }
  constexpr explicit operator bool() const { return has_value(); }

  TRef value() const {
    DCHECK(has_value());
    return TRef(data_, false);
  }
  TRef operator*() const { return value(); }
  ArrowOperatorHelper operator->() const {
    return ArrowOperatorHelper(value());
  }

  bool equals(OptionalRef other) const { return data_ == other.data_; }

  size_t hash_value() const {
    return has_value() ? value().hash_value() : base::hash_value(0);
  }

 private:
  explicit OptionalRef(ObjectData* data) : data_(data) {
    CHECK_NOT_NULL(data_);
  }
  ObjectData* data_ = nullptr;

  template <typename SRef>
  friend class OptionalRef;
};

template <typename T>
inline bool operator==(OptionalRef<T> lhs, OptionalRef<T> rhs) {
  return lhs.equals(rhs);
}

template <typename T>
inline size_t hash_value(OptionalRef<T> ref) {
  return ref.hash_value();
}

// Define aliases for OptionalFooRef = OptionalRef<FooRef>.
#define V(Name) using Optional##Name##Ref = OptionalRef<Name##Ref>;
V(Object)
HEAP_BROKER_OBJECT_LIST(V)
#undef V

class V8_EXPORT_PRIVATE ObjectRef {
 public:
  explicit ObjectRef(ObjectData* data, bool check_type = true) : data_(data) {
    CHECK_NOT_NULL(data_);
  }

  Handle<Object> object() const;

  bool equals(ObjectRef other) const;

  size_t hash_value() const { return base::hash_combine(object().address()); }

  bool IsSmi() const;
  int AsSmi() const;

#define HEAP_IS_METHOD_DECL(Name) bool Is##Name() const;
  HEAP_BROKER_OBJECT_LIST(HEAP_IS_METHOD_DECL)
#undef HEAP_IS_METHOD_DECL

#define HEAP_AS_METHOD_DECL(Name) Name##Ref As##Name() const;
  HEAP_BROKER_OBJECT_LIST(HEAP_AS_METHOD_DECL)
#undef HEAP_AS_METHOD_DECL

  bool IsNull() const;
  bool IsUndefined() const;
  enum HoleType HoleType() const;
  bool IsTheHole() const;
  bool IsPropertyCellHole() const;
  bool IsHashTableHole() const;
  bool IsPromiseHole() const;
  bool IsNullOrUndefined() const;

  std::optional<bool> TryGetBooleanValue(JSHeapBroker* broker) const;
  Maybe<double> OddballToNumber(JSHeapBroker* broker) const;

  bool should_access_heap() const;

  ObjectData* data() const;

  struct Hash {
    size_t operator()(ObjectRef ref) const { return ref.hash_value(); }
  };

 protected:
  ObjectData* data_;  // Should be used only by object() getters.

 private:
  friend class FunctionTemplateInfoRef;
  friend class JSArrayData;
  friend class JSFunctionData;
  friend class JSGlobalObjectData;
  friend class JSGlobalObjectRef;
  friend class JSHeapBroker;
  friend class JSObjectData;
  friend class StringData;

  template <typename TRef>
  friend class OptionalRef;

  friend std::ostream& operator<<(std::ostream& os, ObjectRef ref);
  friend bool operator<(ObjectRef lhs, ObjectRef rhs);
  template <typename T, typename Enable>
  friend struct ::v8::internal::ZoneCompactSetTraits;
};

inline bool operator==(ObjectRef lhs, ObjectRef rhs) { return lhs.equals(rhs); }

inline bool operator!=(ObjectRef lhs, ObjectRef rhs) {
  return !lhs.equals(rhs);
}

inline bool operator<(ObjectRef lhs, ObjectRef rhs) {
  return lhs.data_ < rhs.data_;
}

inline size_t hash_value(ObjectRef ref) { return ref.hash_value(); }

template <class T>
using ZoneRefUnorderedSet = ZoneUnorderedSet<T, ObjectRef::Hash>;

template <class K, class V>
using ZoneRefMap = ZoneMap<K, V>;

// Temporary class that carries information from a Map. We'd like to remove
// this class and use MapRef instead, but we can't as long as we support the
// kDisabled broker mode. That's because obtaining the MapRef via
// HeapObjectRef::map() requires a HandleScope when the broker is disabled.
// During OptimizeGraph we generally don't have a HandleScope, however. There
// are two places where we therefore use GetHeapObjectType() instead. Both that
// function and this class should eventually be removed.
class HeapObjectType {
 public:
  enum Flag : uint8_t { kUndetectable = 1 << 0, kCallable = 1 << 1 };

  using Flags = base::Flags<Flag>;

  HeapObjectType(InstanceType instance_type, ElementsKind elements_kind,
                 Flags flags, OddballType oddball_type, HoleType hole_type)
      : instance_type_(instance_type),
        elements_kind_(elements_kind),
        oddball_type_(oddball_type),
        hole_type_(hole_type),
        flags_(flags) {
    DCHECK_EQ(instance_type == ODDBALL_TYPE,
              oddball_type != OddballType::kNone);
  }

  OddballType oddball_type() const { return oddball_type_; }
  HoleType hole_type() const { return hole_type_; }
  // For compatibility with MapRef.
  OddballType oddball_type(JSHeapBroker* broker) const { return oddball_type_; }
  HoleType hole_type(JSHeapBroker* broker) const { return hole_type_; }
  InstanceType instance_type() const { return instance_type_; }
  Flags flags() const { return flags_; }
  ElementsKind elements_kind() const { return elements_kind_; }

  bool is_callable() const { return flags_ & kCallable; }
  bool is_undetectable() const { return flags_ & kUndetectable; }

 private:
  InstanceType const instance_type_;
  ElementsKind const elements_kind_;
  OddballType const oddball_type_;
  HoleType const hole_type_;
  Flags const flags_;
};

// Constructors are carefully defined such that we do a type check on
// the outermost Ref class in the inheritance chain only.
#define DEFINE_REF_CONSTRUCTOR(Name, Base)                     \
  explicit Name##Ref(ObjectData* data, bool check_type = true) \
      : Base(data, false) {                                    \
    if (check_type) {                                          \
      CHECK(Is##Name());                                       \
    }                                                          \
  }

class HeapObjectRef : public ObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(HeapObject, ObjectRef)

  Handle<HeapObject> object() const;

  MapRef map(JSHeapBroker* broker) const;

  // Only for use in special situations where we need to read the object's
  // current map (instead of returning the cached map). Use with care.
  OptionalMapRef map_direct_read(JSHeapBroker* broker) const;

  // See the comment on the HeapObjectType class.
  HeapObjectType GetHeapObjectType(JSHeapBroker* broker) const;
};

class PropertyCellRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(PropertyCell, HeapObjectRef)

  Handle<PropertyCell> object() const;

  V8_WARN_UNUSED_RESULT bool Cache(JSHeapBroker* broker) const;
  void CacheAsProtector(JSHeapBroker* broker) const {
    bool cached = Cache(broker);
    // A protector always holds a Smi value and its cell type never changes, so
    // Cache can't fail.
    CHECK(cached);
  }

  PropertyDetails property_details() const;
  ObjectRef value(JSHeapBroker* broker) const;
};

class JSReceiverRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSReceiver, HeapObjectRef)

  Handle<JSReceiver> object() const;
};

class JSObjectRef : public JSReceiverRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSObject, JSReceiverRef)

  Handle<JSObject> object() const;

  OptionalObjectRef raw_properties_or_hash(JSHeapBroker* broker) const;

  // Usable only for in-object properties. Only use this if the underlying
  // value can be an uninitialized-sentinel. Otherwise, use the higher-level
  // GetOwnFastConstantDataProperty/GetOwnFastConstantDoubleProperty.
  OptionalObjectRef RawInobjectPropertyAt(JSHeapBroker* broker,
                                          FieldIndex index) const;

  // Return the element at key {index} if {index} is known to be an own data
  // property of the object that is non-writable and non-configurable. If
  // {dependencies} is non-null, a dependency will be taken to protect
  // against inconsistency due to weak memory concurrency.
  OptionalObjectRef GetOwnConstantElement(
      JSHeapBroker* broker, FixedArrayBaseRef elements_ref, uint32_t index,
      CompilationDependencies* dependencies) const;
  // The direct-read implementation of the above, extracted into a helper since
  // it's also called from compilation-dependency validation. This helper is
  // guaranteed to not create new Ref instances.
  std::optional<Tagged<Object>> GetOwnConstantElementFromHeap(
      JSHeapBroker* broker, Tagged<FixedArrayBase> elements,
      ElementsKind elements_kind, uint32_t index) const;

  // Return the value of the property identified by the field {index}
  // if {index} is known to be an own data property of the object and the field
  // is constant.
  // If a property was successfully read, then the function will take a
  // dependency to check the value of the property at code finalization time.
  //
  // This is *not* allowed to be a double representation field. Those should use
  // GetOwnFastDoubleProperty, to avoid unnecessary HeapNumber allocation.
  OptionalObjectRef GetOwnFastConstantDataProperty(
      JSHeapBroker* broker, Representation field_representation,
      FieldIndex index, CompilationDependencies* dependencies) const;

  // Return the value of the double property identified by the field {index}
  // if {index} is known to be an own data property of the object and the field
  // is constant.
  // If a property was successfully read, then the function will take a
  // dependency to check the value of the property at code finalization time.
  std::optional<Float64> GetOwnFastConstantDoubleProperty(
      JSHeapBroker* broker, FieldIndex index,
      CompilationDependencies* dependencies) const;

  // Return the value of the dictionary property at {index} in the dictionary
  // if {index} is known to be an own data property of the object.
  OptionalObjectRef GetOwnDictionaryProperty(
      JSHeapBroker* broker, InternalIndex index,
      CompilationDependencies* dependencies) const;

  // When concurrent inlining is enabled, reads the elements through a direct
  // relaxed read. This is to ease the transition to unserialized (or
  // background-serialized) elements.
  OptionalFixedArrayBaseRef elements(JSHeapBroker* broker,
                                     RelaxedLoadTag) const;
  bool IsElementsTenured(FixedArrayBaseRef elements);

  OptionalMapRef GetObjectCreateMap(JSHeapBroker* broker) const;
};

class JSDataViewRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSDataView, JSObjectRef)

  Handle<JSDataView> object() const;

  size_t byte_length() const;
};

class JSBoundFunctionRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSBoundFunction, JSObjectRef)

  Handle<JSBoundFunction> object() const;

  JSReceiverRef bound_target_function(JSHeapBroker* broker) const;
  ObjectRef bound_this(JSHeapBroker* broker) const;
  FixedArrayRef bound_arguments(JSHeapBroker* broker) const;
};

class V8_EXPORT_PRIVATE JSFunctionRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSFunction, JSObjectRef)

  Handle<JSFunction> object() const;

  // Returns true, iff the serialized JSFunctionData contents are consistent
  // with the state of the underlying JSFunction object. Must be called from
  // the main thread.
  bool IsConsistentWithHeapState(JSHeapBroker* broker) const;

  ContextRef context(JSHeapBroker* broker) const;
  NativeContextRef native_context(JSHeapBroker* broker) const;
  SharedFunctionInfoRef shared(JSHeapBroker* broker) const;
  OptionalCodeRef code(JSHeapBroker* broker) const;

  bool has_initial_map(JSHeapBroker* broker) const;
  bool PrototypeRequiresRuntimeLookup(JSHeapBroker* broker) const;
  bool has_instance_prototype(JSHeapBroker* broker) const;
  HeapObjectRef instance_prototype(JSHeapBroker* broker) const;
  MapRef initial_map(JSHeapBroker* broker) const;
  int InitialMapInstanceSizeWithMinSlack(JSHeapBroker* broker) const;
  FeedbackCellRef raw_feedback_cell(JSHeapBroker* broker) const;
  OptionalFeedbackVectorRef feedback_vector(JSHeapBroker* broker) const;
};

class RegExpBoilerplateDescriptionRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(RegExpBoilerplateDescription, HeapObjectRef)

  Handle<RegExpBoilerplateDescription> object() const;

  HeapObjectRef data(JSHeapBroker* broker) const;
  StringRef source(JSHeapBroker* broker) const;
  int flags() const;
};

// HeapNumberRef is only created for immutable HeapNumbers. Mutable
// HeapNumbers (those owned by in-object or backing store fields with
// representation type Double are not exposed to the compiler through
// HeapNumberRef. Instead, we read their value, and protect that read
// with a field-constness Dependency.
class HeapNumberRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(HeapNumber, HeapObjectRef)

  Handle<HeapNumber> object() const;

  double value() const;
  uint64_t value_as_bits() const;
};

class ContextRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(Context, HeapObjectRef)

  Handle<Context> object() const;

  // {previous} decrements {depth} by 1 for each previous link successfully
  // followed. If {depth} != 0 on function return, then it only got partway to
  // the desired depth.
  ContextRef previous(JSHeapBroker* broker, size_t* depth) const;

  // Only returns a value if the index is valid for this ContextRef.
  OptionalObjectRef get(JSHeapBroker* broker, int index) const;

  ScopeInfoRef scope_info(JSHeapBroker* broker) const;

  // Only returns a value if the index is valid for this ContextRef.
  OptionalObjectRef TryGetSideData(JSHeapBroker* broker, int index) const;
};

#define BROKER_NATIVE_CONTEXT_FIELDS(V)          \
  V(JSFunction, array_function)                  \
  V(JSFunction, bigint_function)                 \
  V(JSFunction, boolean_function)                \
  V(JSFunction, function_prototype_apply)        \
  V(JSFunction, number_function)                 \
  V(JSFunction, object_function)                 \
  V(JSFunction, promise_function)                \
  V(JSFunction, promise_then)                    \
  V(JSFunction, regexp_exec_function)            \
  V(JSFunction, regexp_function)                 \
  V(JSFunction, string_function)                 \
  V(JSFunction, symbol_function)                 \
  V(JSGlobalObject, global_object)               \
  V(JSGlobalProxy, global_proxy_object)          \
  V(JSObject, initial_array_prototype)           \
  V(JSObject, promise_prototype)                 \
  V(Map, async_function_object_map)              \
  V(Map, block_context_map)                      \
  V(Map, bound_function_with_constructor_map)    \
  V(Map, bound_function_without_constructor_map) \
  V(Map, catch_context_map)                      \
  V(Map, eval_context_map)                       \
  V(Map, fast_aliased_arguments_map)             \
  V(Map, function_context_map)                   \
  V(Map, initial_array_iterator_map)             \
  V(Map, initial_string_iterator_map)            \
  V(Map, iterator_result_map)                    \
  V(Map, js_array_holey_double_elements_map)     \
  V(Map, js_array_holey_elements_map)            \
  V(Map, js_array_holey_smi_elements_map)        \
  V(Map, js_array_packed_double_elements_map)    \
  V(Map, js_array_packed_elements_map)           \
  V(Map, js_array_packed_smi_elements_map)       \
  V(Map, map_key_iterator_map)                   \
  V(Map, map_key_value_iterator_map)             \
  V(Map, map_value_iterator_map)                 \
  V(Map, meta_map)                               \
  V(Map, set_key_value_iterator_map)             \
  V(Map, set_value_iterator_map)                 \
  V(Map, sloppy_arguments_map)                   \
  V(Map, slow_object_with_null_prototype_map)    \
  V(Map, strict_arguments_map)                   \
  V(Map, with_context_map)                       \
  V(ScriptContextTable, script_context_table)

class NativeContextRef : public ContextRef {
 public:
  DEFINE_REF_CONSTRUCTOR(NativeContext, ContextRef)

  Handle<NativeContext> object() const;

#define DECL_ACCESSOR(type, name) type##Ref name(JSHeapBroker* broker) const;
  BROKER_NATIVE_CONTEXT_FIELDS(DECL_ACCESSOR)
#undef DECL_ACCESSOR

  MapRef GetFunctionMapFromIndex(JSHeapBroker* broker, int index) const;
  MapRef GetInitialJSArrayMap(JSHeapBroker* broker, ElementsKind kind) const;
  OptionalJSFunctionRef GetConstructorFunction(JSHeapBroker* broker,
                                               MapRef map) const;
  bool GlobalIsDetached(JSHeapBroker* broker) const;
};

class NameRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(Name, HeapObjectRef)

  Handle<Name> object() const;

  bool IsUniqueName() const;
};

class DescriptorArrayRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(DescriptorArray, HeapObjectRef)

  Handle<DescriptorArray> object() const;

  PropertyDetails GetPropertyDetails(InternalIndex descriptor_index) const;
  NameRef GetPropertyKey(JSHeapBroker* broker,
                         InternalIndex descriptor_index) const;
  OptionalObjectRef GetStrongValue(JSHeapBroker* broker,
                                   InternalIndex descriptor_index) const;
};

class FeedbackCellRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(FeedbackCell, HeapObjectRef)

  Handle<FeedbackCell> object() const;

  ObjectRef value(JSHeapBroker* broker) const;

  // Convenience wrappers around {value()}:
  OptionalFeedbackVectorRef feedback_vector(JSHeapBroker* broker) const;
  OptionalSharedFunctionInfoRef shared_function_info(
      JSHeapBroker* broker) const;
};

class FeedbackVectorRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(FeedbackVector, HeapObjectRef)

  Handle<FeedbackVector> object() const;

  SharedFunctionInfoRef shared_function_info(JSHeapBroker* broker) const;

  FeedbackCellRef GetClosureFeedbackCell(JSHeapBroker* broker, int index) const;

  bool was_once_deoptimized() const;
};

class AccessorInfoRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(AccessorInfo, HeapObjectRef)

  Handle<AccessorInfo> object() const;
};

class AllocationSiteRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(AllocationSite, HeapObjectRef)

  Handle<AllocationSite> object() const;

  bool PointsToLiteral() const;
  AllocationType GetAllocationType() const;
  ObjectRef nested_site(JSHeapBroker* broker) const;

  OptionalJSObjectRef boilerplate(JSHeapBroker* broker) const;
  ElementsKind GetElementsKind() const;
  bool CanInlineCall() const;
};

class BigIntRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(BigInt, HeapObjectRef)

  Handle<BigInt> object() const;

  uint64_t AsUint64() const;
  int64_t AsInt64(bool* lossless) const;
};

class V8_EXPORT_PRIVATE MapRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(Map, HeapObjectRef)

  Handle<Map> object() const;

  int instance_size() const;
  InstanceType instance_type() const;
  int GetInObjectProperties() const;
  int GetInObjectPropertiesStartInWords() const;
  int NumberOfOwnDescriptors() const;
  int GetInObjectPropertyOffset(int index) const;
  int constructor_function_index() const;
  int NextFreePropertyIndex() const;
  int UnusedPropertyFields() const;
  ElementsKind elements_kind() const;
  bool is_stable() const;
  bool is_constructor() const;
  bool has_prototype_slot() const;
  bool is_access_check_needed() const;
  bool is_deprecated() const;
  bool CanBeDeprecated() const;
  bool CanTransition() const;
  bool IsInobjectSlackTrackingInProgress() const;
  bool is_dictionary_map() const;
  bool IsFixedCowArrayMap(JSHeapBroker* broker) const;
  bool IsPrimitiveMap() const;
  bool is_undetectable() const;
  bool is_callable() const;
  bool has_indexed_interceptor() const;
  int construction_counter() const;
  bool is_migration_target() const;
  bool supports_fast_array_iteration(JSHeapBroker* broker) const;
  bool supports_fast_array_resize(JSHeapBroker* broker) const;
  bool is_abandoned_prototype_map() const;

  OddballType oddball_type(JSHeapBroker* broker) const;

  bool CanInlineElementAccess() const;

  // Note: Only returns a value if the requested elements kind matches the
  // current kind, or if the current map is an unmodified JSArray initial map.
  OptionalMapRef AsElementsKind(JSHeapBroker* broker, ElementsKind kind) const;

#define DEF_TESTER(Type, ...) bool Is##Type##Map() const;
  INSTANCE_TYPE_CHECKERS(DEF_TESTER)
#undef DEF_TESTER

  bool IsBooleanMap(JSHeapBroker* broker) const;

  HeapObjectRef GetBackPointer(JSHeapBroker* broker) const;

  HeapObjectRef prototype(JSHeapBroker* broker) const;

  bool PrototypesElementsDoNotHaveAccessorsOrThrow(
      JSHeapBroker* broker, ZoneVector<MapRef>* prototype_maps);

  // Concerning the underlying instance_descriptors:
  DescriptorArrayRef instance_descriptors(JSHeapBroker* broker) const;
  MapRef FindFieldOwner(JSHeapBroker* broker,
                        InternalIndex descriptor_index) const;
  PropertyDetails GetPropertyDetails(JSHeapBroker* broker,
                                     InternalIndex descriptor_index) const;
  NameRef GetPropertyKey(JSHeapBroker* broker,
                         InternalIndex descriptor_index) const;
  FieldIndex GetFieldIndexFor(InternalIndex descriptor_index) const;
  OptionalObjectRef GetStrongValue(JSHeapBroker* broker,
                                   InternalIndex descriptor_number) const;

  MapRef FindRootMap(JSHeapBroker* broker) const;
  ObjectRef GetConstructor(JSHeapBroker* broker) const;
};

struct HolderLookupResult {
  HolderLookupResult(CallOptimization::HolderLookup lookup_ =
                         CallOptimization::kHolderNotFound,
                     OptionalJSObjectRef holder_ = std::nullopt)
      : lookup(lookup_), holder(holder_) {}
  CallOptimization::HolderLookup lookup;
  OptionalJSObjectRef holder;
};

class FunctionTemplateInfoRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(FunctionTemplateInfo, HeapObjectRef)

  Handle<FunctionTemplateInfo> object() const;

  bool is_signature_undefined(JSHeapBroker* broker) const;
  bool accept_any_receiver() const;
  int16_t allowed_receiver_instance_type_range_start() const;
  int16_t allowed_receiver_instance_type_range_end() const;

  // Function pointer and a data value that should be passed to the callback.
  // The |callback_data| must be read before the |callback|.
  Address callback(JSHeapBroker* broker) const;
  OptionalObjectRef callback_data(JSHeapBroker* broker) const;

  ZoneVector<Address> c_functions(JSHeapBroker* broker) const;
  ZoneVector<const CFunctionInfo*> c_signatures(JSHeapBroker* broker) const;
  HolderLookupResult LookupHolderOfExpectedType(JSHeapBroker* broker,
                                                MapRef receiver_map);
};

class FixedArrayBaseRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(FixedArrayBase, HeapObjectRef)

  Handle<FixedArrayBase> object() const;

  int length() const;
};

class ArrayBoilerplateDescriptionRef : public HeapObjectRef {
 public:
  using HeapObjectRef::HeapObjectRef;
  Handle<ArrayBoilerplateDescription> object() const;

  int constants_elements_length() const;
};

class FixedArrayRef : public FixedArrayBaseRef {
 public:
  DEFINE_REF_CONSTRUCTOR(FixedArray, FixedArrayBaseRef)

  Handle<FixedArray> object() const;

  OptionalObjectRef TryGet(JSHeapBroker* broker, int i) const;
};

class FixedDoubleArrayRef : public FixedArrayBaseRef {
 public:
  DEFINE_REF_CONSTRUCTOR(FixedDoubleArray, FixedArrayBaseRef)

  Handle<FixedDoubleArray> object() const;

  // Due to 64-bit unaligned reads, only usable for
  // immutable-after-initialization FixedDoubleArrays protected by
  // acquire-release semantics (such as boilerplate elements).
  Float64 GetFromImmutableFixedDoubleArray(int i) const;
};

class BytecodeArrayRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(BytecodeArray, HeapObjectRef)

  Handle<BytecodeArray> object() const;

  // NOTE: Concurrent reads of the actual bytecodes as well as the constant pool
  // (both immutable) do not go through BytecodeArrayRef but are performed
  // directly through the handle by BytecodeArrayIterator.

  int length() const;

  int register_count() const;
  uint16_t parameter_count() const;
  uint16_t max_arguments() const;
  interpreter::Register incoming_new_target_or_generator_register() const;

  Handle<TrustedByteArray> SourcePositionTable(JSHeapBroker* broker) const;

  // Exception handler table.
  Address handler_table_address() const;
  int handler_table_size() const;
};

class ScriptContextTableRef : public FixedArrayBaseRef {
 public:
  DEFINE_REF_CONSTRUCTOR(ScriptContextTable, FixedArrayBaseRef)

  Handle<ScriptContextTable> object() const;
};

class ObjectBoilerplateDescriptionRef : public FixedArrayRef {
 public:
  DEFINE_REF_CONSTRUCTOR(ObjectBoilerplateDescription, FixedArrayRef)

  Handle<ObjectBoilerplateDescription> object() const;

  int boilerplate_properties_count() const;
};

class JSArrayRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSArray, JSObjectRef)

  Handle<JSArray> object() const;

  // The `length` property of boilerplate JSArray objects. Boilerplates are
  // immutable after initialization. Must not be used for non-boilerplate
  // JSArrays.
  ObjectRef GetBoilerplateLength(JSHeapBroker* broker) const;

  // Return the element at key {index} if the array has a copy-on-write elements
  // storage and {index} is known to be an own data property.
  // Note the value returned by this function is only valid if we ensure at
  // runtime that the backing store has not changed.
  OptionalObjectRef GetOwnCowElement(JSHeapBroker* broker,
                                     FixedArrayBaseRef elements_ref,
                                     uint32_t index) const;

  // The `JSArray::length` property; not safe to use in general, but can be
  // used in some special cases that guarantee a valid `length` value despite
  // concurrent reads. The result needs to be optional in case the
  // return value was created too recently to pass the gc predicate.
  OptionalObjectRef length_unsafe(JSHeapBroker* broker) const;
};

class ScopeInfoRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(ScopeInfo, HeapObjectRef)

  Handle<ScopeInfo> object() const;

  int ContextLength() const;
  bool HasContext() const;
  bool HasOuterScopeInfo() const;
  bool HasContextExtensionSlot() const;
  bool ClassScopeHasPrivateBrand() const;
  ScopeType scope_type() const;

  ScopeInfoRef OuterScopeInfo(JSHeapBroker* broker) const;
};

#define BROKER_SFI_FIELDS(V)                                    \
  V(int, internal_formal_parameter_count_with_receiver)         \
  V(int, internal_formal_parameter_count_without_receiver)      \
  V(bool, IsDontAdaptArguments)                                 \
  V(bool, has_simple_parameters)                                \
  V(bool, has_duplicate_parameters)                             \
  V(int, function_map_index)                                    \
  V(FunctionKind, kind)                                         \
  V(LanguageMode, language_mode)                                \
  V(bool, native)                                               \
  V(bool, HasBuiltinId)                                         \
  V(bool, construct_as_builtin)                                 \
  V(bool, HasBytecodeArray)                                     \
  V(int, StartPosition)                                         \
  V(bool, is_compiled)                                          \
  V(bool, IsUserJavaScript)                                     \
  V(bool, requires_instance_members_initializer)                \
  IF_WASM(V, const wasm::WasmModule*, wasm_module)              \
  IF_WASM(V, const wasm::FunctionSig*, wasm_function_signature) \
  IF_WASM(V, int, wasm_function_index)

class V8_EXPORT_PRIVATE SharedFunctionInfoRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(SharedFunctionInfo, HeapObjectRef)

  Handle<SharedFunctionInfo> object() const;

  Builtin builtin_id() const;
  int context_header_size() const;
  int context_parameters_start() const;
  BytecodeArrayRef GetBytecodeArray(JSHeapBroker* broker) const;
  bool HasBreakInfo(JSHeapBroker* broker) const;
  SharedFunctionInfo::Inlineability GetInlineability(
      JSHeapBroker* broker) const;
  OptionalFunctionTemplateInfoRef function_template_info(
      JSHeapBroker* broker) const;
  ScopeInfoRef scope_info(JSHeapBroker* broker) const;

#define DECL_ACCESSOR(type, name) type name() const;
  BROKER_SFI_FIELDS(DECL_ACCESSOR)
#undef DECL_ACCESSOR

  bool IsInlineable(JSHeapBroker* broker) const {
    return GetInlineability(broker) == SharedFunctionInfo::kIsInlineable;
  }
};

class StringRef : public NameRef {
 public:
  DEFINE_REF_CONSTRUCTOR(String, NameRef)

  Handle<String> object() const;

  // With concurrent inlining on, we return std::nullopt due to not being able
  // to use LookupIterator in a thread-safe way.
  OptionalObjectRef GetCharAsStringOrUndefined(JSHeapBroker* broker,
                                               uint32_t index) const;

  // When concurrently accessing non-read-only non-supported strings, we return
  // std::nullopt for these methods.
  std::optional<Handle<String>> ObjectIfContentAccessible(JSHeapBroker* broker);
  int length() const;
  std::optional<uint16_t> GetFirstChar(JSHeapBroker* broker) const;
  std::optional<uint16_t> GetChar(JSHeapBroker* broker, int index) const;
  std::optional<double> ToNumber(JSHeapBroker* broker);
  std::optional<double> ToInt(JSHeapBroker* broker, int radix);

  bool IsSeqString() const;
  bool IsExternalString() const;

  bool IsContentAccessible() const;
  bool IsOneByteRepresentation() const;

 private:
  // With concurrent inlining on, we currently support reading directly
  // internalized strings, and thin strings (which are pointers to internalized
  // strings).
  bool SupportedStringKind() const;
};

class SymbolRef : public NameRef {
 public:
  DEFINE_REF_CONSTRUCTOR(Symbol, NameRef)

  Handle<Symbol> object() const;
};

class JSTypedArrayRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSTypedArray, JSObjectRef)

  Handle<JSTypedArray> object() const;

  bool is_on_heap() const;
  size_t length() const;
  void* data_ptr() const;
  HeapObjectRef buffer(JSHeapBroker* broker) const;
};

class JSPrimitiveWrapperRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSPrimitiveWrapper, JSObjectRef)

  bool IsStringWrapper(JSHeapBroker* broker) const;

  Handle<JSPrimitiveWrapper> object() const;
};

class SourceTextModuleRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(SourceTextModule, HeapObjectRef)

  Handle<SourceTextModule> object() const;

  OptionalCellRef GetCell(JSHeapBroker* broker, int cell_index) const;
  OptionalObjectRef import_meta(JSHeapBroker* broker) const;
};

class TemplateObjectDescriptionRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(TemplateObjectDescription, HeapObjectRef)

  Handle<TemplateObjectDescription> object() const;
};

class CellRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(Cell, HeapObjectRef)

  Handle<Cell> object() const;
};

class JSGlobalObjectRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSGlobalObject, JSObjectRef)

  Handle<JSGlobalObject> object() const;

  bool IsDetachedFrom(JSGlobalProxyRef proxy) const;

  // Can be called even when there is no property cell for the given name.
  OptionalPropertyCellRef GetPropertyCell(JSHeapBroker* broker,
                                          NameRef name) const;
};

class JSGlobalProxyRef : public JSObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(JSGlobalProxy, JSObjectRef)

  Handle<JSGlobalProxy> object() const;
};

class CodeRef : public HeapObjectRef {
 public:
  DEFINE_REF_CONSTRUCTOR(Code, HeapObjectRef)

  Handle<Code> object() const;

  unsigned GetInlinedBytecodeSize() const;
};

class InternalizedStringRef : public StringRef {
 public:
  DEFINE_REF_CONSTRUCTOR(InternalizedString, StringRef)

  Handle<InternalizedString> object() const;
};

#undef DEFINE_REF_CONSTRUCTOR

#define V(Name)                                                   \
  /* Refs should contain only one pointer. */                     \
  static_assert(sizeof(Name##Ref) == kSystemPointerSize);         \
  static_assert(sizeof(OptionalName##Ref) == kSystemPointerSize); \
  /* Refs should be trivial to copy, move and destroy. */         \
  static_assert(std::is_trivially_copyable_v<Name##Ref>);         \
  static_assert(std::is_trivially_copyable_v<OptionalName##Ref>); \
  static_assert(std::is_trivially_destructible_v<Name##Ref>);     \
  static_assert(std::is_trivially_destructible_v<OptionalName##Ref>);

V(Object) HEAP_BROKER_OBJECT_LIST(V)
#undef V

}  // namespace compiler

template <typename T>
struct ZoneCompactSetTraits<T, std::enable_if_t<compiler::is_ref<T>::value>> {
  using handle_type = T;
  using data_type = compiler::ObjectData;

  static data_type* HandleToPointer(handle_type handle) {
    return handle.data();
  }
  static handle_type PointerToHandle(data_type* ptr) {
    return handle_type(ptr);
  }
};

namespace compiler {

template <typename T>
using ZoneRefSet = ZoneCompactSet<typename ref_traits<T>::ref_type>;

inline bool AnyMapIsHeapNumber(const ZoneRefSet<Map>& maps) {
  return std::any_of(maps.begin(), maps.end(),
                     [](MapRef map) { return map.IsHeapNumberMap(); });
}

inline bool AnyMapIsHeapNumber(const base::Vector<const MapRef>& maps) {
  return std::any_of(maps.begin(), maps.end(),
                     [](MapRef map) { return map.IsHeapNumberMap(); });
}

}  // namespace compiler

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_HEAP_REFS_H_

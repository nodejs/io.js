// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MAP_H_
#define V8_OBJECTS_MAP_H_

#include <optional>

#include "include/v8-memory-span.h"
#include "src/base/bit-field.h"
#include "src/common/globals.h"
#include "src/objects/code.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-checker.h"
#include "src/objects/internal-index.h"
#include "src/objects/objects.h"
#include "src/objects/prototype-info.h"
#include "src/roots/roots.h"
#include "torque-generated/bit-fields.h"
#include "torque-generated/visitor-lists.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

class WasmTypeInfo;

enum InstanceType : uint16_t;

#define DATA_ONLY_VISITOR_ID_LIST(V) \
  V(BigInt)                          \
  V(CoverageInfo)                    \
  V(FeedbackMetadata)                \
  V(Filler)                          \
  V(HeapNumber)                      \
  V(SeqOneByteString)                \
  V(SeqTwoByteString)                \
  IF_WASM(V, WasmNull)

#define POINTER_VISITOR_ID_LIST(V)   \
  V(AccessorInfo)                    \
  V(AllocationSite)                  \
  V(BytecodeWrapper)                 \
  V(CallSiteInfo)                    \
  V(Cell)                            \
  V(CodeWrapper)                     \
  V(ConsString)                      \
  V(ConstTrackingLetCell)            \
  V(DataHandler)                     \
  V(DebugInfo)                       \
  V(EmbedderDataArray)               \
  V(EphemeronHashTable)              \
  V(ExternalPointerArray)            \
  V(ExternalString)                  \
  V(FeedbackCell)                    \
  V(Foreign)                         \
  V(FreeSpace)                       \
  V(FunctionTemplateInfo)            \
  V(Hole)                            \
  V(JSApiObject)                     \
  V(JSArrayBuffer)                   \
  V(JSDataViewOrRabGsabDataView)     \
  V(JSDate)                          \
  V(JSExternalObject)                \
  V(JSFinalizationRegistry)          \
  V(JSFunction)                      \
  V(JSObject)                        \
  V(JSObjectFast)                    \
  V(JSRegExp)                        \
  V(JSSynchronizationPrimitive)      \
  V(JSTypedArray)                    \
  V(JSWeakCollection)                \
  V(JSWeakRef)                       \
  V(Map)                             \
  V(NativeContext)                   \
  V(Oddball)                         \
  V(PreparseData)                    \
  V(PropertyArray)                   \
  V(PropertyCell)                    \
  V(PrototypeInfo)                   \
  V(RegExpBoilerplateDescription)    \
  V(RegExpDataWrapper)               \
  V(SharedFunctionInfo)              \
  V(ShortcutCandidate)               \
  V(SlicedString)                    \
  V(SloppyArgumentsElements)         \
  V(SmallOrderedHashMap)             \
  V(SmallOrderedHashSet)             \
  V(SmallOrderedNameDictionary)      \
  V(SourceTextModule)                \
  V(Struct)                          \
  V(SwissNameDictionary)             \
  V(Symbol)                          \
  V(SyntheticModule)                 \
  V(ThinString)                      \
  V(TransitionArray)                 \
  IF_WASM(V, WasmArray)              \
  IF_WASM(V, WasmContinuationObject) \
  IF_WASM(V, WasmFuncRef)            \
  IF_WASM(V, WasmGlobalObject)       \
  IF_WASM(V, WasmInstanceObject)     \
  IF_WASM(V, WasmResumeData)         \
  IF_WASM(V, WasmStruct)             \
  IF_WASM(V, WasmSuspenderObject)    \
  IF_WASM(V, WasmSuspendingObject)   \
  IF_WASM(V, WasmTableObject)        \
  IF_WASM(V, WasmTagObject)          \
  IF_WASM(V, WasmTypeInfo)           \
  V(WeakCell)                        \
  SIMPLE_HEAP_OBJECT_LIST1(V)

#define TORQUE_VISITOR_ID_LIST(V)     \
  TORQUE_DATA_ONLY_VISITOR_ID_LIST(V) \
  TORQUE_POINTER_VISITOR_ID_LIST(V)

#define TRUSTED_VISITOR_ID_LIST(V) CONCRETE_TRUSTED_OBJECT_TYPE_LIST1(V)

// Objects with the same visitor id are processed in the same way by
// the heap visitors. The visitor ids for data only objects must precede
// other visitor ids. We rely on kDataOnlyVisitorIdCount for quick check
// of whether an object contains only data or may contain pointers.
enum VisitorId {
#define VISITOR_ID_ENUM_DECL(id) kVisit##id,
  // clang-format off
  DATA_ONLY_VISITOR_ID_LIST(VISITOR_ID_ENUM_DECL)
  TORQUE_DATA_ONLY_VISITOR_ID_LIST(VISITOR_ID_ENUM_DECL)
  kDataOnlyVisitorIdCount,
  POINTER_VISITOR_ID_LIST(VISITOR_ID_ENUM_DECL)
  TORQUE_POINTER_VISITOR_ID_LIST(VISITOR_ID_ENUM_DECL)
  TRUSTED_VISITOR_ID_LIST(VISITOR_ID_ENUM_DECL)
  kVisitorIdCount
// clang-format on
#undef VISITOR_ID_ENUM_DECL
};

enum class ObjectFields {
  kDataOnly,
  kMaybePointers,
};

using MapHandles = std::vector<Handle<Map>>;
using MapHandlesSpan = v8::MemorySpan<Handle<Map>>;

#include "torque-generated/src/objects/map-tq.inc"

// All heap objects have a Map that describes their structure.
//  A Map contains information about:
//  - Size information about the object
//  - How to iterate over an object (for garbage collection)
//
// Map layout:
// +---------------+-------------------------------------------------+
// |   _ Type _    | _ Description _                                 |
// +---------------+-------------------------------------------------+
// | TaggedPointer | map - Always a pointer to the MetaMap root      |
// +---------------+-------------------------------------------------+
// | Int           | The first int field                             |
//  `---+----------+-------------------------------------------------+
//      | Byte     | [instance_size]                                 |
//      +----------+-------------------------------------------------+
//      | Byte     | If Map for a primitive type:                    |
//      |          |   native context index for constructor fn       |
//      |          | If Map for an Object type:                      |
//      |          |   inobject properties start offset in words     |
//      +----------+-------------------------------------------------+
//      | Byte     | [used_or_unused_instance_size_in_words]         |
//      |          | For JSObject in fast mode this byte encodes     |
//      |          | the size of the object that includes only       |
//      |          | the used property fields or the slack size      |
//      |          | in properties backing store.                    |
//      +----------+-------------------------------------------------+
//      | Byte     | [visitor_id]                                    |
// +----+----------+-------------------------------------------------+
// | Int           | The second int field                            |
//  `---+----------+-------------------------------------------------+
//      | Short    | [instance_type]                                 |
//      +----------+-------------------------------------------------+
//      | Byte     | [bit_field]                                     |
//      |          |   - has_non_instance_prototype (bit 0)          |
//      |          |   - is_callable (bit 1)                         |
//      |          |   - has_named_interceptor (bit 2)               |
//      |          |   - has_indexed_interceptor (bit 3)             |
//      |          |   - is_undetectable (bit 4)                     |
//      |          |   - is_access_check_needed (bit 5)              |
//      |          |   - is_constructor (bit 6)                      |
//      |          |   - has_prototype_slot (bit 7)                  |
//      +----------+-------------------------------------------------+
//      | Byte     | [bit_field2]                                    |
//      |          |   - new_target_is_base (bit 0)                  |
//      |          |   - is_immutable_proto (bit 1)                  |
//      |          |   - elements_kind (bits 2..7)                   |
// +----+----------+-------------------------------------------------+
// | Int           | [bit_field3]                                    |
// |               |   - enum_length (bit 0..9)                      |
// |               |   - number_of_own_descriptors (bit 10..19)      |
// |               |   - is_prototype_map (bit 20)                   |
// |               |   - is_dictionary_map (bit 21)                  |
// |               |   - owns_descriptors (bit 22)                   |
// |               |   - is_in_retained_map_list (bit 23)            |
// |               |   - is_deprecated (bit 24)                      |
// |               |   - is_unstable (bit 25)                        |
// |               |   - is_migration_target (bit 26)                |
// |               |   - is_extensible (bit 28)                      |
// |               |   - may_have_interesting_properties (bit 28)    |
// |               |   - construction_counter (bit 29..31)           |
// |               |                                                 |
// +*****************************************************************+
// | Int           | On systems with 64bit pointer types, there      |
// |               | is an unused 32bits after bit_field3            |
// +*****************************************************************+
// | TaggedPointer | [prototype]                                     |
// +---------------+-------------------------------------------------+
// | TaggedPointer | [constructor_or_back_pointer_or_native_context] |
// +---------------+-------------------------------------------------+
// | TaggedPointer | [instance_descriptors]                          |
// +*****************************************************************+
// | TaggedPointer | [dependent_code]                                |
// +---------------+-------------------------------------------------+
// | TaggedPointer | [prototype_validity_cell]                       |
// +---------------+-------------------------------------------------+
// | TaggedPointer | If Map is a prototype map:                      |
// |               |   [prototype_info]                              |
// |               | Else:                                           |
// |               |   [raw_transitions]                             |
// +---------------+-------------------------------------------------+

class Map : public TorqueGeneratedMap<Map, HeapObject> {
 public:
  // Instance size.
  // Size in bytes or kVariableSizeSentinel if instances do not have
  // a fixed size.
  DECL_INT_ACCESSORS(instance_size)
  // Size in words or kVariableSizeSentinel if instances do not have
  // a fixed size.
  DECL_INT_ACCESSORS(instance_size_in_words)

  // [inobject_properties_start_or_constructor_function_index]:
  // Provides access to the inobject properties start offset in words in case of
  // JSObject maps, or the constructor function index in case of primitive maps.
  DECL_INT_ACCESSORS(inobject_properties_start_or_constructor_function_index)

  // Get/set the in-object property area start offset in words in the object.
  inline int GetInObjectPropertiesStartInWords() const;
  inline void SetInObjectPropertiesStartInWords(int value);
  // Count of properties allocated in the object (JSObject only).
  inline int GetInObjectProperties() const;
  // Index of the constructor function in the native context (primitives only),
  // or the special sentinel value to indicate that there is no object wrapper
  // for the primitive (i.e. in case of null or undefined).
  static const int kNoConstructorFunctionIndex = 0;
  inline int GetConstructorFunctionIndex() const;
  inline void SetConstructorFunctionIndex(int value);
  static std::optional<Tagged<JSFunction>> GetConstructorFunction(
      Tagged<Map> map, Tagged<Context> native_context);

  // Retrieve interceptors.
  DECL_GETTER(GetNamedInterceptor, Tagged<InterceptorInfo>)
  DECL_GETTER(GetIndexedInterceptor, Tagged<InterceptorInfo>)

  // Instance type.
  DECL_PRIMITIVE_ACCESSORS(instance_type, InstanceType)

  // Returns the size of the used in-object area including object header
  // (only used for JSObject in fast mode, for the other kinds of objects it
  // is equal to the instance size).
  inline int UsedInstanceSize() const;

  inline bool HasOutOfObjectProperties() const;

  // Tells how many unused property fields (in-object or out-of object) are
  // available in the instance (only used for JSObject in fast mode).
  inline int UnusedPropertyFields() const;
  // Tells how many unused in-object property words are present.
  inline int UnusedInObjectProperties() const;
  // Updates the counters tracking unused fields in the object.
  inline void SetInObjectUnusedPropertyFields(int unused_property_fields);
  // Updates the counters tracking unused fields in the property array.
  inline void SetOutOfObjectUnusedPropertyFields(int unused_property_fields);
  inline void CopyUnusedPropertyFields(Tagged<Map> map);
  inline void CopyUnusedPropertyFieldsAdjustedForInstanceSize(Tagged<Map> map);
  inline void AccountAddedPropertyField();
  inline void AccountAddedOutOfObjectPropertyField(
      int unused_in_property_array);

  //
  // Bit field.
  //
  // The setter in this pair calls the relaxed setter if concurrent marking is
  // on, or performs the write non-atomically if it's off. The read is always
  // non-atomically. This is done to have wider TSAN coverage on the cases where
  // it's possible.
  DECL_PRIMITIVE_ACCESSORS(bit_field, uint8_t)

  // Atomic accessors, used for allowlisting legitimate concurrent accesses.
  DECL_PRIMITIVE_ACCESSORS(relaxed_bit_field, uint8_t)

  // Bit positions for |bit_field|.
  struct Bits1 {
    DEFINE_TORQUE_GENERATED_MAP_BIT_FIELDS1()
  };

  //
  // Bit field 2.
  //
  DECL_PRIMITIVE_ACCESSORS(bit_field2, uint8_t)

  // Bit positions for |bit_field2|.
  struct Bits2 {
    DEFINE_TORQUE_GENERATED_MAP_BIT_FIELDS2()
  };

  //
  // Bit field 3.
  //
  // {bit_field3} calls the relaxed accessors if concurrent marking is on, or
  // performs the read/write non-atomically if it's off. This is done to have
  // wider TSAN coverage on the cases where it's possible.
  DECL_PRIMITIVE_ACCESSORS(bit_field3, uint32_t)

  DECL_PRIMITIVE_ACCESSORS(relaxed_bit_field3, uint32_t)
  DECL_PRIMITIVE_ACCESSORS(release_acquire_bit_field3, uint32_t)

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic. Depending on the V8 build mode there could be no padding.
  V8_INLINE void clear_padding();

  // Bit positions for |bit_field3|.
  struct Bits3 {
    DEFINE_TORQUE_GENERATED_MAP_BIT_FIELDS3()
  };

  // Ensure that Torque-defined bit widths for |bit_field3| are as expected.
  static_assert(Bits3::EnumLengthBits::kSize == kDescriptorIndexBitCount);
  static_assert(Bits3::NumberOfOwnDescriptorsBits::kSize ==
                kDescriptorIndexBitCount);

  static_assert(Bits3::NumberOfOwnDescriptorsBits::kMax >=
                kMaxNumberOfDescriptors);

  static const int kSlackTrackingCounterStart = 7;
  static const int kSlackTrackingCounterEnd = 1;
  static const int kNoSlackTracking = 0;
  static_assert(kSlackTrackingCounterStart <=
                Bits3::ConstructionCounterBits::kMax);

  // Inobject slack tracking is the way to reclaim unused inobject space.
  //
  // The instance size is initially determined by adding some slack to
  // expected_nof_properties (to allow for a few extra properties added
  // after the constructor). There is no guarantee that the extra space
  // will not be wasted.
  //
  // Here is the algorithm to reclaim the unused inobject space:
  // - Detect the first constructor call for this JSFunction.
  //   When it happens enter the "in progress" state: initialize construction
  //   counter in the initial_map.
  // - While the tracking is in progress initialize unused properties of a new
  //   object with one_pointer_filler_map instead of undefined_value (the "used"
  //   part is initialized with undefined_value as usual). This way they can
  //   be resized quickly and safely.
  // - Once enough objects have been created  compute the 'slack'
  //   (traverse the map transition tree starting from the
  //   initial_map and find the lowest value of unused_property_fields).
  // - Traverse the transition tree again and decrease the instance size
  //   of every map. Existing objects will resize automatically (they are
  //   filled with one_pointer_filler_map). All further allocations will
  //   use the adjusted instance size.
  // - SharedFunctionInfo's expected_nof_properties left unmodified since
  //   allocations made using different closures could actually create different
  //   kind of objects (see prototype inheritance pattern).
  //
  //  Important: inobject slack tracking is not attempted during the snapshot
  //  creation.

  static const int kGenerousAllocationCount =
      kSlackTrackingCounterStart - kSlackTrackingCounterEnd + 1;

  // Starts the tracking by initializing object constructions countdown counter.
  void StartInobjectSlackTracking();

  // True if the object constructions countdown counter is a range
  // [kSlackTrackingCounterEnd, kSlackTrackingCounterStart].
  inline bool IsInobjectSlackTrackingInProgress() const;

  // Does the tracking step.
  inline void InobjectSlackTrackingStep(Isolate* isolate);

  // Computes inobject slack for the transition tree starting at this initial
  // map.
  int ComputeMinObjectSlack(Isolate* isolate);
  inline int InstanceSizeFromSlack(int slack) const;

  // Tells whether the object in the prototype property will be used
  // for instances created from this function.  If the prototype
  // property is set to a value that is not a JSObject, the prototype
  // property will not be used to create instances of the function.
  // See ECMA-262, 13.2.2.
  DECL_BOOLEAN_ACCESSORS(has_non_instance_prototype)

  // Tells whether the instance has a [[Construct]] internal method.
  // This property is implemented according to ES6, section 7.2.4.
  DECL_BOOLEAN_ACCESSORS(is_constructor)

  // Tells whether the instance with this map may have properties for
  // interesting symbols on it.
  // An "interesting symbol" is one for which Name::IsInteresting()
  // returns true, i.e. a well-known symbol like @@toStringTag.
  DECL_BOOLEAN_ACCESSORS(may_have_interesting_properties)

  DECL_BOOLEAN_ACCESSORS(has_prototype_slot)

  // Records and queries whether the instance has a named interceptor.
  DECL_BOOLEAN_ACCESSORS(has_named_interceptor)

  // Records and queries whether the instance has an indexed interceptor.
  DECL_BOOLEAN_ACCESSORS(has_indexed_interceptor)

  // Tells whether the instance is undetectable.
  // An undetectable object is a special class of JSObject: 'typeof' operator
  // returns undefined, ToBoolean returns false. Otherwise it behaves like
  // a normal JS object.  It is useful for implementing undetectable
  // document.all in Firefox & Safari.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=248549.
  DECL_BOOLEAN_ACCESSORS(is_undetectable)

  // Tells whether the instance has a [[Call]] internal method.
  // This property is implemented according to ES6, section 7.2.3.
  DECL_BOOLEAN_ACCESSORS(is_callable)

  DECL_BOOLEAN_ACCESSORS(new_target_is_base)
  DECL_BOOLEAN_ACCESSORS(is_extensible)
  DECL_BOOLEAN_ACCESSORS(is_prototype_map)
  inline bool is_abandoned_prototype_map() const;
  inline bool has_prototype_info() const;
  inline bool TryGetPrototypeInfo(Tagged<PrototypeInfo>* result) const;

  // Whether the instance has been added to the retained map list by
  // Heap::AddRetainedMap.
  DECL_BOOLEAN_ACCESSORS(is_in_retained_map_list)

  DECL_PRIMITIVE_ACCESSORS(elements_kind, ElementsKind)

  // Tells whether the instance has fast elements that are only Smis.
  inline bool has_fast_smi_elements() const;

  // Tells whether the instance has fast elements.
  inline bool has_fast_object_elements() const;
  inline bool has_fast_smi_or_object_elements() const;
  inline bool has_fast_double_elements() const;
  inline bool has_fast_elements() const;
  inline bool has_fast_packed_elements() const;
  inline bool has_sloppy_arguments_elements() const;
  inline bool has_fast_sloppy_arguments_elements() const;
  inline bool has_fast_string_wrapper_elements() const;
  inline bool has_typed_array_or_rab_gsab_typed_array_elements() const;
  inline bool has_any_typed_array_or_wasm_array_elements() const;
  inline bool has_dictionary_elements() const;
  inline bool has_any_nonextensible_elements() const;
  inline bool has_nonextensible_elements() const;
  inline bool has_sealed_elements() const;
  inline bool has_frozen_elements() const;
  inline bool has_shared_array_elements() const;

  // Weakly checks whether a map is detached from all transition trees. If this
  // returns true, the map is guaranteed to be detached. If it returns false,
  // there is no guarantee it is attached.
  inline bool IsDetached(Isolate* isolate) const;

  // Returns true if there is an object with potentially read-only elements
  // in the prototype chain. It could be a Proxy, a string wrapper,
  // an object with DICTIONARY_ELEMENTS potentially containing read-only
  // elements or an object with any frozen elements, or a slow arguments object.
  bool ShouldCheckForReadOnlyElementsInPrototypeChain(Isolate* isolate);

  inline Tagged<Map> ElementsTransitionMap(Isolate* isolate,
                                           ConcurrencyMode cmode);

  inline Tagged<FixedArrayBase> GetInitialElements() const;

  // [raw_transitions]: Provides access to the transitions storage field.
  // Don't call set_raw_transitions() directly to overwrite transitions, use
  // the TransitionArray::ReplaceTransitions() wrapper instead!
  DECL_ACCESSORS(raw_transitions, Tagged<MaybeObject>)
  DECL_RELEASE_ACQUIRE_WEAK_ACCESSORS(raw_transitions)
  // [prototype_info]: Per-prototype metadata. Aliased with transitions
  // (which prototype maps don't have).
  DECL_GETTER(prototype_info, Tagged<Object>)
  DECL_RELEASE_ACQUIRE_ACCESSORS(prototype_info, Tagged<Object>)
  // PrototypeInfo is created lazily using this helper (which installs it on
  // the given prototype's map).
  static Handle<PrototypeInfo> GetOrCreatePrototypeInfo(
      DirectHandle<JSObject> prototype, Isolate* isolate);
  static Handle<PrototypeInfo> GetOrCreatePrototypeInfo(
      DirectHandle<Map> prototype_map, Isolate* isolate);
  inline bool should_be_fast_prototype_map() const;
  static void SetShouldBeFastPrototypeMap(DirectHandle<Map> map, bool value,
                                          Isolate* isolate);

  // [prototype chain validity cell]: Associated with a prototype object,
  // stored in that object's map, indicates that prototype chains through this
  // object are currently valid. The cell will be invalidated and replaced when
  // the prototype chain changes. When there's nothing to guard (for example,
  // when direct prototype is null or Proxy) this function returns Smi with
  // |kPrototypeChainValid| sentinel value.
  static Handle<UnionOf<Smi, Cell>> GetOrCreatePrototypeChainValidityCell(
      DirectHandle<Map> map, Isolate* isolate);
  static const int kPrototypeChainValid = 0;
  static const int kPrototypeChainInvalid = 1;

  static bool IsPrototypeChainInvalidated(Tagged<Map> map);

  // Return the map of the root of object's prototype chain.
  Tagged<Map> GetPrototypeChainRootMap(Isolate* isolate) const;

  V8_EXPORT_PRIVATE Tagged<Map> FindRootMap(PtrComprCageBase cage_base) const;
  V8_EXPORT_PRIVATE Tagged<Map> FindFieldOwner(PtrComprCageBase cage_base,
                                               InternalIndex descriptor) const;

  inline int GetInObjectPropertyOffset(int index) const;

  class FieldCounts {
   public:
    FieldCounts(int mutable_count, int const_count)
        : mutable_count_(mutable_count), const_count_(const_count) {}

    int GetTotal() const { return mutable_count() + const_count(); }

    int mutable_count() const { return mutable_count_; }
    int const_count() const { return const_count_; }

   private:
    int mutable_count_;
    int const_count_;
  };

  FieldCounts GetFieldCounts() const;
  int NumberOfFields(ConcurrencyMode cmode) const;

  // TODO(ishell): candidate with JSObject::MigrateToMap().
  bool InstancesNeedRewriting(Tagged<Map> target, ConcurrencyMode cmode) const;
  bool InstancesNeedRewriting(Tagged<Map> target, int target_number_of_fields,
                              int target_inobject, int target_unused,
                              int* old_number_of_fields,
                              ConcurrencyMode cmode) const;
  // Returns true if the |field_type| is the most general one for
  // given |representation|.
  static inline bool IsMostGeneralFieldType(Representation representation,
                                            Tagged<FieldType> field_type);

  // Generalizes representation and field_type if objects with given
  // instance type can have fast elements that can be transitioned by
  // stubs or optimized code to more general elements kind.
  // This generalization is necessary in order to ensure that elements kind
  // transitions performed by stubs / optimized code don't silently transition
  // fields with representation "Tagged" back to "Smi" or "HeapObject" or
  // fields with HeapObject representation and "Any" type back to "Class" type.
  static inline void GeneralizeIfCanHaveTransitionableFastElementsKind(
      Isolate* isolate, InstanceType instance_type,
      Representation* representation, Handle<FieldType>* field_type);

  V8_EXPORT_PRIVATE static Handle<Map> PrepareForDataProperty(
      Isolate* isolate, Handle<Map> old_map, InternalIndex descriptor_number,
      PropertyConstness constness, DirectHandle<Object> value);

  V8_EXPORT_PRIVATE static Handle<Map> Normalize(
      Isolate* isolate, Handle<Map> map, ElementsKind new_elements_kind,
      Handle<HeapObject> new_prototype, PropertyNormalizationMode mode,
      bool use_cache, const char* reason);
  V8_EXPORT_PRIVATE static Handle<Map> Normalize(
      Isolate* isolate, Handle<Map> map, ElementsKind new_elements_kind,
      Handle<HeapObject> new_prototype, PropertyNormalizationMode mode,
      const char* reason) {
    const bool kUseCache = true;
    return Normalize(isolate, map, new_elements_kind, new_prototype, mode,
                     kUseCache, reason);
  }

  inline static Handle<Map> Normalize(Isolate* isolate, Handle<Map> fast_map,
                                      PropertyNormalizationMode mode,
                                      const char* reason);

  // Tells whether the map is used for JSObjects in dictionary mode (ie
  // normalized objects, ie objects for which HasFastProperties returns false).
  // A map can never be used for both dictionary mode and fast mode JSObjects.
  // False by default and for HeapObjects that are not JSObjects.
  DECL_BOOLEAN_ACCESSORS(is_dictionary_map)

  // Tells whether the instance needs security checks when accessing its
  // properties.
  DECL_BOOLEAN_ACCESSORS(is_access_check_needed)

  // [prototype]: implicit prototype object.
  DECL_ACCESSORS(prototype, Tagged<HeapObject>)
  // TODO(jkummerow): make set_prototype private.
  V8_EXPORT_PRIVATE static void SetPrototype(
      Isolate* isolate, DirectHandle<Map> map, Handle<HeapObject> prototype,
      bool enable_prototype_setup_mode = true);

  // Sets prototype and constructor fields to null. Can be called during
  // bootstrapping.
  inline void init_prototype_and_constructor_or_back_pointer(
      ReadOnlyRoots roots);

  // [constructor]: points back to the function or FunctionTemplateInfo
  // responsible for this map.
  // The field overlaps with the back pointer. All maps in a transition tree
  // have the same constructor, so maps with back pointers can walk the
  // back pointer chain until they find the map holding their constructor.
  // Returns null_value if there's neither a constructor function nor a
  // FunctionTemplateInfo available.
  // The field also overlaps with the native context pointer for context maps,
  // and with the Wasm type info for WebAssembly object maps.
  DECL_ACCESSORS(constructor_or_back_pointer, Tagged<Object>)
  DECL_RELAXED_ACCESSORS(constructor_or_back_pointer, Tagged<Object>)
  DECL_ACCESSORS(native_context, Tagged<NativeContext>)
  DECL_ACCESSORS(native_context_or_null, Tagged<Object>)
  DECL_GETTER(raw_native_context_or_null, Tagged<Object>)
  DECL_ACCESSORS(wasm_type_info, Tagged<WasmTypeInfo>)

  // Gets |constructor_or_back_pointer| field value from the root map.
  // The result might be null, JSFunction, FunctionTemplateInfo or a Tuple2
  // for JSFunctions with non-instance prototypes.
  DECL_GETTER(GetConstructorRaw, Tagged<Object>)

  // Gets constructor value from the root map. Unwraps Tuple2 in case of
  // JSFunction map with non-instance prototype.
  // The result returned might be null, JSFunction or FunctionTemplateInfo.
  DECL_GETTER(GetConstructor, Tagged<Object>)
  DECL_GETTER(GetFunctionTemplateInfo, Tagged<FunctionTemplateInfo>)
  inline void SetConstructor(Tagged<Object> constructor,
                             WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  // Constructor getter that performs at most the given number of steps
  // in the transition tree. Returns either the constructor or the map at
  // which the walk has stopped.
  inline Tagged<Object> TryGetConstructor(PtrComprCageBase cage_base,
                                          int max_steps);

  // Gets non-instance prototype value which is stored in Tuple2 in a
  // root map's |constructor_or_back_pointer| field.
  DECL_GETTER(GetNonInstancePrototype, Tagged<Object>)

  // [back pointer]: points back to the parent map from which a transition
  // leads to this map. The field overlaps with the constructor (see above).
  DECL_GETTER(GetBackPointer, Tagged<HeapObject>)
  inline void SetBackPointer(Tagged<HeapObject> value,
                             WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline bool TryGetBackPointer(PtrComprCageBase cage_base,
                                Tagged<Map>* back_pointer) const;

  // [instance descriptors]: describes the object.
  DECL_ACCESSORS(instance_descriptors, Tagged<DescriptorArray>)
  DECL_RELAXED_ACCESSORS(instance_descriptors, Tagged<DescriptorArray>)
  DECL_ACQUIRE_GETTER(instance_descriptors, Tagged<DescriptorArray>)
  V8_EXPORT_PRIVATE void SetInstanceDescriptors(
      Isolate* isolate, Tagged<DescriptorArray> descriptors,
      int number_of_own_descriptors);

  inline void UpdateDescriptors(Isolate* isolate,
                                Tagged<DescriptorArray> descriptors,
                                int number_of_own_descriptors);
  inline void InitializeDescriptors(Isolate* isolate,
                                    Tagged<DescriptorArray> descriptors);

  // [dependent code]: list of optimized codes that weakly embed this map.
  DECL_ACCESSORS(dependent_code, Tagged<DependentCode>)

  // [prototype_validity_cell]: Cell containing the validity bit for prototype
  // chains or Tagged<Smi>(0) if uninitialized.
  // The meaning of this validity cell is different for prototype maps and
  // non-prototype maps.
  // For prototype maps the validity bit "guards" modifications of prototype
  // chains going through this object. When a prototype object changes, both its
  // own validity cell and those of all "downstream" prototypes are invalidated;
  // handlers for a given receiver embed the currently valid cell for that
  // receiver's prototype during their creation and check it on execution.
  // For non-prototype maps which are used as transitioning store handlers this
  // field contains the validity cell which guards modifications of this map's
  // prototype.
  DECL_RELAXED_ACCESSORS(prototype_validity_cell, Tagged<Object>)

  // Returns true if prototype validity cell value represents "valid" prototype
  // chain state.
  inline bool IsPrototypeValidityCellValid() const;

  inline Tagged<Name> GetLastDescriptorName(Isolate* isolate) const;
  inline PropertyDetails GetLastDescriptorDetails(Isolate* isolate) const;

  inline InternalIndex LastAdded() const;

  inline int NumberOfOwnDescriptors() const;
  inline void SetNumberOfOwnDescriptors(int number);
  inline InternalIndex::Range IterateOwnDescriptors() const;

  inline Tagged<Cell> RetrieveDescriptorsPointer();

  // Checks whether all properties are stored either in the map or on the object
  // (inobject, properties, or elements backing store), requiring no special
  // checks.
  bool OnlyHasSimpleProperties() const;
  inline int EnumLength() const;
  inline void SetEnumLength(int length);

  DECL_BOOLEAN_ACCESSORS(owns_descriptors)

  inline void mark_unstable();
  inline bool is_stable() const;

  DECL_BOOLEAN_ACCESSORS(is_migration_target)

  DECL_BOOLEAN_ACCESSORS(is_immutable_proto)

  // This counter is used for in-object slack tracking.
  // The in-object slack tracking is considered enabled when the counter is
  // non zero. The counter only has a valid count for initial maps. For
  // transitioned maps only kNoSlackTracking has a meaning, namely that inobject
  // slack tracking already finished for the transition tree. Any other value
  // indicates that either inobject slack tracking is still in progress, or that
  // the map isn't part of the transition tree anymore.
  DECL_INT_ACCESSORS(construction_counter)

  DECL_BOOLEAN_ACCESSORS(is_deprecated)
  inline bool CanBeDeprecated() const;

  // Returns a non-deprecated version of the input. If the input was not
  // deprecated, it is directly returned. Otherwise, the non-deprecated version
  // is found by re-transitioning from the root of the transition tree using the
  // descriptor array of the map. Returns MaybeHandle<Map>() if no updated map
  // is found.
  V8_EXPORT_PRIVATE static MaybeHandle<Map> TryUpdate(
      Isolate* isolate, Handle<Map> map) V8_WARN_UNUSED_RESULT;

  // Returns a non-deprecated version of the input. This method may deprecate
  // existing maps along the way if encodings conflict. Not for use while
  // gathering type feedback. Use TryUpdate in those cases instead.
  V8_EXPORT_PRIVATE static Handle<Map> Update(Isolate* isolate,
                                              Handle<Map> map);

  static inline Handle<Map> CopyInitialMap(Isolate* isolate, Handle<Map> map);
  V8_EXPORT_PRIVATE static Handle<Map> CopyInitialMap(
      Isolate* isolate, Handle<Map> map, int instance_size,
      int in_object_properties, int unused_property_fields);
  static Handle<Map> CopyInitialMapNormalized(
      Isolate* isolate, Handle<Map> map,
      PropertyNormalizationMode mode = CLEAR_INOBJECT_PROPERTIES);
  static Handle<Map> CopyDropDescriptors(Isolate* isolate, Handle<Map> map);
  V8_EXPORT_PRIVATE static Handle<Map> CopyInsertDescriptor(
      Isolate* isolate, Handle<Map> map, Descriptor* descriptor,
      TransitionFlag flag);

  static MaybeObjectHandle WrapFieldType(Handle<FieldType> type);
  V8_EXPORT_PRIVATE static Tagged<FieldType> UnwrapFieldType(
      Tagged<MaybeObject> wrapped_type);

  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Map> CopyWithField(
      Isolate* isolate, Handle<Map> map, Handle<Name> name,
      Handle<FieldType> type, PropertyAttributes attributes,
      PropertyConstness constness, Representation representation,
      TransitionFlag flag);

  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Map>
  CopyWithConstant(Isolate* isolate, Handle<Map> map, Handle<Name> name,
                   DirectHandle<Object> constant, PropertyAttributes attributes,
                   TransitionFlag flag);

  // Returns a new map with all transitions dropped from the given map and
  // the ElementsKind set.
  static Handle<Map> TransitionElementsTo(Isolate* isolate, Handle<Map> map,
                                          ElementsKind to_kind);

  static std::optional<Tagged<Map>> TryAsElementsKind(Isolate* isolate,
                                                      DirectHandle<Map> map,
                                                      ElementsKind kind,
                                                      ConcurrencyMode cmode);
  V8_EXPORT_PRIVATE static Handle<Map> AsElementsKind(Isolate* isolate,
                                                      DirectHandle<Map> map,
                                                      ElementsKind kind);

  static Handle<Map> CopyAsElementsKind(Isolate* isolate, Handle<Map> map,
                                        ElementsKind kind, TransitionFlag flag);

  static Handle<Map> AsLanguageMode(
      Isolate* isolate, Handle<Map> initial_map,
      DirectHandle<SharedFunctionInfo> shared_info);

  V8_EXPORT_PRIVATE static Handle<Map> CopyForPreventExtensions(
      Isolate* isolate, Handle<Map> map, PropertyAttributes attrs_to_add,
      Handle<Symbol> transition_marker, const char* reason,
      bool old_map_is_dictionary_elements_kind = false);

  // Maximal number of fast properties. Used to restrict the number of map
  // transitions to avoid an explosion in the number of maps for objects used as
  // dictionaries.
  inline bool TooManyFastProperties(StoreOrigin store_origin) const;
  V8_EXPORT_PRIVATE static Handle<Map> TransitionToDataProperty(
      Isolate* isolate, Handle<Map> map, Handle<Name> name,
      DirectHandle<Object> value, PropertyAttributes attributes,
      PropertyConstness constness, StoreOrigin store_origin);
  V8_EXPORT_PRIVATE static Handle<Map> TransitionToAccessorProperty(
      Isolate* isolate, Handle<Map> map, Handle<Name> name,
      InternalIndex descriptor, DirectHandle<Object> getter,
      DirectHandle<Object> setter, PropertyAttributes attributes);

  inline void AppendDescriptor(Isolate* isolate, Descriptor* desc);

  // Returns a copy of the map, prepared for inserting into the transition
  // tree (if the |map| owns descriptors then the new one will share
  // descriptors with |map|).
  static Handle<Map> CopyForElementsTransition(Isolate* isolate,
                                               Handle<Map> map);

  // Returns a copy of the map, prepared for inserting into the transition
  // tree as a prototype transition.
  static Handle<Map> CopyForPrototypeTransition(Isolate* isolate,
                                                Handle<Map> map,
                                                Handle<HeapObject> prototype);

  // Returns a copy of the map, with all transitions dropped from the
  // instance descriptors.
  static Handle<Map> Copy(Isolate* isolate, Handle<Map> map, const char* reason,
                          TransitionKindFlag kind = SPECIAL_TRANSITION);
  V8_EXPORT_PRIVATE static Handle<Map> Create(Isolate* isolate,
                                              int inobject_properties);

  // Returns the next free property index (only valid for FAST MODE).
  int NextFreePropertyIndex() const;

  // Returns the number of enumerable properties.
  int NumberOfEnumerableProperties() const;

  static inline int SlackForArraySize(int old_size, int size_limit);

  V8_EXPORT_PRIVATE static void EnsureDescriptorSlack(Isolate* isolate,
                                                      DirectHandle<Map> map,
                                                      int slack);

  // Returns the map to be used for instances when the given {prototype} is
  // passed to an Object.create call. Might transition the given {prototype}.
  static Handle<Map> GetObjectCreateMap(Isolate* isolate,
                                        Handle<HeapObject> prototype);

  // Returns the map to be used for instances when the given {prototype} is
  // passed to Reflect.construct or proxy constructors.
  static Handle<Map> GetDerivedMap(Isolate* isolate, Handle<Map> from,
                                   Handle<JSReceiver> prototype);

  // Computes a hash value for this map, to be used in HashTables and such.
  int Hash();
  // Compute the hash assuming another prototype.
  int Hash(Tagged<HeapObject> prototype);

  // Returns the transitioned map for this map with the most generic
  // elements_kind that's found in |candidates|, or |nullptr| if no match is
  // found at all.
  V8_EXPORT_PRIVATE Tagged<Map> FindElementsKindTransitionedMap(
      Isolate* isolate, MapHandlesSpan candidates, ConcurrencyMode cmode);

  inline bool CanTransition() const;

  static constexpr std::optional<RootIndex> TryGetMapRootIdxFor(
      InstanceType type) {
    switch (type) {
#define MAKE_CASE(TYPE, Name, name) \
  case TYPE:                        \
    return RootIndex::k##Name##Map;
      STRUCT_LIST(MAKE_CASE)
      TORQUE_DEFINED_INSTANCE_TYPE_LIST(MAKE_CASE)
#undef MAKE_CASE
      default:
        break;
    }
    return {};
  }
  static inline Tagged<Map> GetMapFor(ReadOnlyRoots roots, InstanceType type);

  bool IsMapInArrayPrototypeChain(Isolate* isolate) const;

  // Dispatched behavior.
  void MapPrint(std::ostream& os);
  DECL_VERIFIER(Map)

#ifdef VERIFY_HEAP
  void DictionaryMapVerify(Isolate* isolate);
#endif

  DECL_PRIMITIVE_ACCESSORS(visitor_id, VisitorId)

  static constexpr ObjectFields ObjectFieldsFrom(VisitorId visitor_id) {
    return (visitor_id < kDataOnlyVisitorIdCount)
               ? ObjectFields::kDataOnly
               : ObjectFields::kMaybePointers;
  }

  V8_EXPORT_PRIVATE static Handle<Map> TransitionRootMapToPrototypeForNewObject(
      Isolate* isolate, Handle<Map> map, Handle<HeapObject> prototype);
  V8_EXPORT_PRIVATE static Handle<Map> TransitionToUpdatePrototype(
      Isolate* isolate, Handle<Map> map, Handle<HeapObject> prototype);

  static Handle<Map> TransitionToImmutableProto(Isolate* isolate,
                                                Handle<Map> map);

  static_assert(kInstanceTypeOffset == Internals::kMapInstanceTypeOffset);

  class BodyDescriptor;

  // Compares this map to another to see if they describe equivalent objects,
  // up to the given |elements_kind| and |prototype|. If |mode| is set to
  // CLEAR_INOBJECT_PROPERTIES, |other| is treated as if it had exactly zero
  // inobject properties. The "shared" flags of both this map and |other| are
  // ignored.
  bool EquivalentToForNormalization(const Tagged<Map> other,
                                    ElementsKind elements_kind,
                                    Tagged<HeapObject> prototype,
                                    PropertyNormalizationMode mode) const;
  inline bool EquivalentToForNormalization(
      const Tagged<Map> other, PropertyNormalizationMode mode) const;

  void PrintMapDetails(std::ostream& os);

  static inline Handle<Map> AddMissingTransitionsForTesting(
      Isolate* isolate, Handle<Map> split_map,
      DirectHandle<DescriptorArray> descriptors);

  // Fires when the layout of an object with a leaf map changes.
  // This includes adding transitions to the leaf map or changing
  // the descriptor array.
  inline void NotifyLeafMapLayoutChange(Isolate* isolate);

  V8_EXPORT_PRIVATE static VisitorId GetVisitorId(Tagged<Map> map);

  // Returns true if objects with given instance type are allowed to have
  // fast transitionable elements kinds. This predicate is used to ensure
  // that objects that can have transitionable fast elements kind will not
  // get in-place generalizable fields because the elements kind transition
  // performed by stubs or optimized code can't properly generalize such
  // fields.
  static inline bool CanHaveFastTransitionableElementsKind(
      InstanceType instance_type);
  inline bool CanHaveFastTransitionableElementsKind() const;

  // Maps for Wasm objects can use certain fields for other purposes.
  inline uint8_t WasmByte1() const;
  inline uint8_t WasmByte2() const;
  inline void SetWasmByte1(uint8_t value);
  inline void SetWasmByte2(uint8_t value);

 private:
  // This byte encodes either the instance size without the in-object slack or
  // the slack size in properties backing store.
  // Let H be JSObject::kHeaderSize / kTaggedSize.
  // If value >= H then:
  //     - all field properties are stored in the object.
  //     - there is no property array.
  //     - value * kTaggedSize is the actual object size without the slack.
  // Otherwise:
  //     - there is no slack in the object.
  //     - the property array has value slack slots.
  // Note that this encoding requires that H = JSObject::kFieldsAdded.
  DECL_INT_ACCESSORS(used_or_unused_instance_size_in_words)

  // Returns the map that this (root) map transitions to if its elements_kind
  // is changed to |elements_kind|, or |nullptr| if no such map is cached yet.
  Tagged<Map> LookupElementsTransitionMap(Isolate* isolate,
                                          ElementsKind elements_kind,
                                          ConcurrencyMode cmode);

  // Tries to replay property transitions starting from this (root) map using
  // the descriptor array of the |map|. The |root_map| is expected to have
  // proper elements kind and therefore elements kinds transitions are not
  // taken by this function. Returns |nullptr| if matching transition map is
  // not found.
  Tagged<Map> TryReplayPropertyTransitions(Isolate* isolate, Tagged<Map> map,
                                           ConcurrencyMode cmode);

  static void ConnectTransition(Isolate* isolate, Handle<Map> parent,
                                Handle<Map> child, Handle<Name> name,
                                TransitionKindFlag transition_kind,
                                bool force_connect = false);

  bool EquivalentToForTransition(
      const Tagged<Map> other, ConcurrencyMode cmode,
      Handle<HeapObject> new_prototype = Handle<HeapObject>()) const;
  bool EquivalentToForElementsKindTransition(const Tagged<Map> other,
                                             ConcurrencyMode cmode) const;
  static Handle<Map> RawCopy(Isolate* isolate, Handle<Map> map,
                             int instance_size, int inobject_properties);
  static Handle<Map> ShareDescriptor(Isolate* isolate, Handle<Map> map,
                                     DirectHandle<DescriptorArray> descriptors,
                                     Descriptor* descriptor);
  V8_EXPORT_PRIVATE static Handle<Map> AddMissingTransitions(
      Isolate* isolate, Handle<Map> map,
      DirectHandle<DescriptorArray> descriptors);
  static void InstallDescriptors(Isolate* isolate, Handle<Map> parent_map,
                                 Handle<Map> child_map,
                                 InternalIndex new_descriptor,
                                 DirectHandle<DescriptorArray> descriptors,
                                 // force_connect is used when copying a map
                                 // tree to enforce transitions being added even
                                 // for (still) seemingly detached maps.
                                 bool force_connect = false);
  static Handle<Map> CopyAddDescriptor(Isolate* isolate, Handle<Map> map,
                                       Descriptor* descriptor,
                                       TransitionFlag flag);
  static Handle<Map> CopyReplaceDescriptors(
      Isolate* isolate, Handle<Map> map,
      DirectHandle<DescriptorArray> descriptors, TransitionFlag flag,
      MaybeHandle<Name> maybe_name, const char* reason,
      TransitionKindFlag transition_kind);

  static Handle<Map> CopyReplaceDescriptor(
      Isolate* isolate, Handle<Map> map,
      DirectHandle<DescriptorArray> descriptors, Descriptor* descriptor,
      InternalIndex index, TransitionFlag flag);
  static Handle<Map> CopyNormalized(Isolate* isolate, Handle<Map> map,
                                    PropertyNormalizationMode mode);

  void DeprecateTransitionTree(Isolate* isolate);

  void ReplaceDescriptors(Isolate* isolate,
                          Tagged<DescriptorArray> new_descriptors);

  // This is the replacement for IsMap() which avoids reading the instance type
  // but compares the object's map against given meta_map, so it can be used
  // concurrently without acquire load.
  V8_INLINE static bool ConcurrentIsHeapObjectWithMap(
      PtrComprCageBase cage_base, Tagged<Object> object, Tagged<Map> meta_map);

  // Use the high-level instance_descriptors/SetInstanceDescriptors instead.
  DECL_RELEASE_SETTER(instance_descriptors, Tagged<DescriptorArray>)

  // Hide inherited accessors from the generated superclass.
  DECL_ACCESSORS(constructor_or_back_pointer_or_native_context, Tagged<Object>)
  DECL_ACCESSORS(transitions_or_prototype_info, Tagged<Object>)

  friend class MapUpdater;
  template <typename ConcreteVisitor>
  friend class MarkingVisitorBase;

  TQ_OBJECT_CONSTRUCTORS(Map)
};

// The cache for maps used by normalized (dictionary mode) objects.
// Such maps do not have property descriptors, so a typical program
// needs very limited number of distinct normalized maps.
class NormalizedMapCache : public WeakFixedArray {
 public:
  NEVER_READ_ONLY_SPACE
  static Handle<NormalizedMapCache> New(Isolate* isolate);

  V8_WARN_UNUSED_RESULT MaybeHandle<Map> Get(DirectHandle<Map> fast_map,
                                             ElementsKind elements_kind,
                                             Tagged<HeapObject> prototype,
                                             PropertyNormalizationMode mode);
  void Set(DirectHandle<Map> fast_map, DirectHandle<Map> normalized_map);

  DECL_VERIFIER(NormalizedMapCache)

 private:
  friend bool IsNormalizedMapCache(Tagged<HeapObject> obj,
                                   PtrComprCageBase cage_base);

  static const int kEntries = 64;

  static inline int GetIndex(Tagged<Map> map, Tagged<HeapObject> prototype);

  // The following declarations hide base class methods.
  Tagged<Object> get(int index);
  void set(int index, Tagged<Object> value);

  OBJECT_CONSTRUCTORS(NormalizedMapCache, WeakFixedArray);
};

#define DECL_TESTER(Type, ...) inline bool Is##Type##Map(Tagged<Map> map);
INSTANCE_TYPE_CHECKERS(DECL_TESTER)
#undef DECL_TESTER
inline bool IsBooleanMap(Tagged<Map> map);
inline bool IsNullOrUndefinedMap(Tagged<Map> map);
inline bool IsPrimitiveMap(Tagged<Map> map);
inline bool IsSpecialReceiverMap(Tagged<Map> map);
inline bool IsCustomElementsReceiverMap(Tagged<Map> map);

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_MAP_H_

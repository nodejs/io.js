// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note 1: Any file that includes this one should include object-macros-undef.h
// at the bottom.

// Note 2: This file is deliberately missing the include guards (the undeffing
// approach wouldn't work otherwise).
//
// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

// The accessors with RELAXED_, ACQUIRE_, and RELEASE_ prefixes should be used
// for fields that can be written to and read from multiple threads at the same
// time. See comments in src/base/atomicops.h for the memory ordering sematics.

#include "src/base/memory.h"

// V8 objects are defined as:
//
//     V8_OBJECT class Foo : public Base {
//       ...
//     } V8_OBJECT_END;
//
// These macros are to enable packing down to 4-byte alignment (i.e. int32
// alignment, since we have int32 fields), and to add warnings which ensure that
// there is no unwanted within-object padding.
#if V8_CC_GNU

#define V8_OBJECT_PUSH                                                    \
  _Pragma("pack(push)") _Pragma("pack(4)") _Pragma("GCC diagnostic push") \
      _Pragma("GCC diagnostic error \"-Wpadded\"")
#define V8_OBJECT_POP _Pragma("pack(pop)") _Pragma("GCC diagnostic pop")

#define V8_OBJECT V8_OBJECT_PUSH

// GCC wants this pragma to be a new statement, but we prefer to have
// V8_OBJECT_END look like part of the definition. Insert a semicolon before the
// pragma to make gcc happy, and use static_assert(true) to swallow the next
// semicolon.
#define V8_OBJECT_END \
  ;                   \
  V8_OBJECT_POP static_assert(true)

#define V8_OBJECT_INNER_CLASS V8_OBJECT_POP
#define V8_OBJECT_INNER_CLASS_END \
  ;                               \
  V8_OBJECT_PUSH static_assert(true)

#elif V8_CC_MSVC
#define V8_OBJECT_PUSH                                           \
  __pragma(pack(push)) __pragma(pack(4)) __pragma(warning(push)) \
      __pragma(warning(default : 4820))
#define V8_OBJECT_POP __pragma(pack(pop)) __pragma(warning(pop))

#define V8_OBJECT V8_OBJECT_PUSH
#define V8_OBJECT_END V8_OBJECT_POP

#define V8_OBJECT_INNER_CLASS V8_OBJECT_POP
#define V8_OBJECT_INNER_CLASS_END V8_OBJECT_PUSH
#else
#error Unsupported compiler
#endif

// Since this changes visibility, it should always be last in a class
// definition.
#define OBJECT_CONSTRUCTORS(Type, ...)                                         \
 public:                                                                       \
  constexpr Type() : __VA_ARGS__() {}                                          \
                                                                               \
  /* For every object, add a `->` operator which returns a pointer to this     \
     object. This will allow smoother transition between T and Tagged<T>. */   \
  Type* operator->() { return this; }                                          \
  const Type* operator->() const { return this; }                              \
                                                                               \
 protected:                                                                    \
  friend class Tagged<Type>;                                                   \
                                                                               \
  /* Special constructor for constexpr construction which allows skipping type \
   * checks. */                                                                \
  explicit constexpr V8_INLINE Type(Address ptr, HeapObject::SkipTypeCheckTag) \
      : __VA_ARGS__(ptr, HeapObject::SkipTypeCheckTag()) {}                    \
                                                                               \
  inline void CheckTypeOnCast();                                               \
  explicit inline Type(Address ptr)

#define OBJECT_CONSTRUCTORS_IMPL(Type, Super)                           \
  inline void Type::CheckTypeOnCast() { SLOW_DCHECK(Is##Type(*this)); } \
  inline Type::Type(Address ptr) : Super(ptr) { CheckTypeOnCast(); }

#define NEVER_READ_ONLY_SPACE   \
  inline Heap* GetHeap() const; \
  inline Isolate* GetIsolate() const;

// TODO(leszeks): Add checks in the factory that we never allocate these
// objects in RO space.
#define NEVER_READ_ONLY_SPACE_IMPL(Type)                                   \
  Heap* Type::GetHeap() const { return GetHeapFromWritableObject(*this); } \
  Isolate* Type::GetIsolate() const {                                      \
    return GetIsolateFromWritableObject(*this);                            \
  }

#define DECL_PRIMITIVE_GETTER(name, type) inline type name() const;

#define DECL_PRIMITIVE_SETTER(name, type) inline void set_##name(type value);

#define DECL_PRIMITIVE_ACCESSORS(name, type) \
  DECL_PRIMITIVE_GETTER(name, type)          \
  DECL_PRIMITIVE_SETTER(name, type)

#define DECL_BOOLEAN_ACCESSORS(name) DECL_PRIMITIVE_ACCESSORS(name, bool)

#define DECL_INT_ACCESSORS(name) DECL_PRIMITIVE_ACCESSORS(name, int)

#define DECL_INT32_ACCESSORS(name) DECL_PRIMITIVE_ACCESSORS(name, int32_t)

#define DECL_SANDBOXED_POINTER_ACCESSORS(name, type) \
  DECL_PRIMITIVE_GETTER(name, type)                  \
  DECL_PRIMITIVE_SETTER(name, type)

#define DECL_UINT16_ACCESSORS(name) DECL_PRIMITIVE_ACCESSORS(name, uint16_t)

#define DECL_INT16_ACCESSORS(name) DECL_PRIMITIVE_ACCESSORS(name, int16_t)

#define DECL_UINT8_ACCESSORS(name) DECL_PRIMITIVE_ACCESSORS(name, uint8_t)

#define DECL_RELAXED_PRIMITIVE_ACCESSORS(name, type) \
  inline type name(RelaxedLoadTag) const;            \
  inline void set_##name(type value, RelaxedStoreTag);

#define DECL_RELAXED_INT32_ACCESSORS(name) \
  DECL_RELAXED_PRIMITIVE_ACCESSORS(name, int32_t)

#define DECL_RELAXED_UINT32_ACCESSORS(name) \
  DECL_RELAXED_PRIMITIVE_ACCESSORS(name, uint32_t)

#define DECL_RELAXED_UINT16_ACCESSORS(name) \
  DECL_RELAXED_PRIMITIVE_ACCESSORS(name, uint16_t)

#define DECL_GETTER(name, type) \
  inline type name() const;     \
  inline type name(PtrComprCageBase cage_base) const;

#define DEF_GETTER(holder, name, type)                       \
  type holder::name() const {                                \
    PtrComprCageBase cage_base = GetPtrComprCageBase(*this); \
    return holder::name(cage_base);                          \
  }                                                          \
  type holder::name(PtrComprCageBase cage_base) const

#define DEF_RELAXED_GETTER(holder, name, type)               \
  type holder::name(RelaxedLoadTag tag) const {              \
    PtrComprCageBase cage_base = GetPtrComprCageBase(*this); \
    return holder::name(cage_base, tag);                     \
  }                                                          \
  type holder::name(PtrComprCageBase cage_base, RelaxedLoadTag) const

#define DEF_ACQUIRE_GETTER(holder, name, type)               \
  type holder::name(AcquireLoadTag tag) const {              \
    PtrComprCageBase cage_base = GetPtrComprCageBase(*this); \
    return holder::name(cage_base, tag);                     \
  }                                                          \
  type holder::name(PtrComprCageBase cage_base, AcquireLoadTag) const

#define DEF_HEAP_OBJECT_PREDICATE(holder, name)            \
  bool name(Tagged<holder> obj) {                          \
    PtrComprCageBase cage_base = GetPtrComprCageBase(obj); \
    return name(obj, cage_base);                           \
  }                                                        \
  bool name(Tagged<holder> obj, PtrComprCageBase cage_base)

#define TQ_FIELD_TYPE(name, tq_type) \
  static constexpr const char* k##name##TqFieldType = tq_type;

#define DECL_FIELD_OFFSET_TQ(name, value, tq_type) \
  static const int k##name##Offset = value;        \
  TQ_FIELD_TYPE(name, tq_type)

#define DECL_SETTER(name, type)      \
  inline void set_##name(type value, \
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

#define DECL_ACCESSORS(name, type) \
  DECL_GETTER(name, type)          \
  DECL_SETTER(name, type)

#define DECL_ACCESSORS_LOAD_TAG(name, type, tag_type) \
  inline type name(tag_type tag) const;               \
  inline type name(PtrComprCageBase cage_base, tag_type) const;

#define DECL_ACCESSORS_STORE_TAG(name, type, tag_type) \
  inline void set_##name(type value, tag_type,         \
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

#define DECL_RELAXED_GETTER(name, type) \
  DECL_ACCESSORS_LOAD_TAG(name, type, RelaxedLoadTag)

#define DECL_RELAXED_SETTER(name, type) \
  DECL_ACCESSORS_STORE_TAG(name, type, RelaxedStoreTag)

#define DECL_RELAXED_ACCESSORS(name, type) \
  DECL_RELAXED_GETTER(name, type)          \
  DECL_RELAXED_SETTER(name, type)

#define DECL_ACQUIRE_GETTER(name, type) \
  DECL_ACCESSORS_LOAD_TAG(name, type, AcquireLoadTag)

#define DECL_RELEASE_SETTER(name, type) \
  DECL_ACCESSORS_STORE_TAG(name, type, ReleaseStoreTag)

#define DECL_RELEASE_ACQUIRE_ACCESSORS(name, type) \
  DECL_ACQUIRE_GETTER(name, type)                  \
  DECL_RELEASE_SETTER(name, type)

#define DECL_RELEASE_ACQUIRE_WEAK_ACCESSORS(name) \
  DECL_ACQUIRE_GETTER(name, Tagged<MaybeObject>)  \
  DECL_RELEASE_SETTER(name, Tagged<MaybeObject>)

#define DECL_CAST(Type)                                      \
  V8_INLINE static Tagged<Type> cast(Tagged<Object> object); \
  V8_INLINE static constexpr Tagged<Type> unchecked_cast(    \
      Tagged<Object> object) {                               \
    return Tagged<Type>::unchecked_cast(object);             \
  }

#define CAST_ACCESSOR(Type)                        \
  Tagged<Type> Type::cast(Tagged<Object> object) { \
    return Tagged<Type>(Type(object.ptr()));       \
  }

#define DEF_PRIMITIVE_ACCESSORS(holder, name, offset, type)     \
  type holder::name() const { return ReadField<type>(offset); } \
  void holder::set_##name(type value) { WriteField<type>(offset, value); }

#define INT_ACCESSORS(holder, name, offset) \
  DEF_PRIMITIVE_ACCESSORS(holder, name, offset, int)

#define INT32_ACCESSORS(holder, name, offset) \
  DEF_PRIMITIVE_ACCESSORS(holder, name, offset, int32_t)

#define UINT16_ACCESSORS(holder, name, offset) \
  DEF_PRIMITIVE_ACCESSORS(holder, name, offset, uint16_t)

#define UINT8_ACCESSORS(holder, name, offset) \
  DEF_PRIMITIVE_ACCESSORS(holder, name, offset, uint8_t)

#define RELAXED_INT32_ACCESSORS(holder, name, offset)       \
  int32_t holder::name(RelaxedLoadTag) const {              \
    return RELAXED_READ_INT32_FIELD(*this, offset);         \
  }                                                         \
  void holder::set_##name(int32_t value, RelaxedStoreTag) { \
    RELAXED_WRITE_INT32_FIELD(*this, offset, value);        \
  }

#define RELAXED_UINT32_ACCESSORS(holder, name, offset)       \
  uint32_t holder::name(RelaxedLoadTag) const {              \
    return RELAXED_READ_UINT32_FIELD(*this, offset);         \
  }                                                          \
  void holder::set_##name(uint32_t value, RelaxedStoreTag) { \
    RELAXED_WRITE_UINT32_FIELD(*this, offset, value);        \
  }

#define RELAXED_UINT16_ACCESSORS(holder, name, offset)       \
  uint16_t holder::name(RelaxedLoadTag) const {              \
    return RELAXED_READ_UINT16_FIELD(*this, offset);         \
  }                                                          \
  void holder::set_##name(uint16_t value, RelaxedStoreTag) { \
    RELAXED_WRITE_UINT16_FIELD(*this, offset, value);        \
  }

#define ACCESSORS_CHECKED2(holder, name, type, offset, get_condition, \
                           set_condition)                             \
  DEF_GETTER(holder, name, type) {                                    \
    type value = TaggedField<type, offset>::load(cage_base, *this);   \
    DCHECK(get_condition);                                            \
    return value;                                                     \
  }                                                                   \
  void holder::set_##name(type value, WriteBarrierMode mode) {        \
    DCHECK(set_condition);                                            \
    TaggedField<type, offset>::store(*this, value);                   \
    CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);            \
  }

#define ACCESSORS_CHECKED(holder, name, type, offset, condition) \
  ACCESSORS_CHECKED2(holder, name, type, offset, condition, condition)

#define ACCESSORS(holder, name, type, offset) \
  ACCESSORS_CHECKED(holder, name, type, offset, true)

// TODO(jgruber): Eventually, all accessors should be ported to the NOCAGE
// variant (which doesn't define a PtrComprCageBase overload). Once that's
// done, remove the cage-ful macros (e.g. ACCESSORS) and rename the cage-less
// macros (e.g. ACCESSORS_NOCAGE).
#define ACCESSORS_NOCAGE(holder, name, type, offset)           \
  type holder::name() const {                                  \
    PtrComprCageBase cage_base = GetPtrComprCageBase(*this);   \
    return TaggedField<type, offset>::load(cage_base, *this);  \
  }                                                            \
  void holder::set_##name(type value, WriteBarrierMode mode) { \
    TaggedField<type, offset>::store(*this, value);            \
    CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);     \
  }

#define RENAME_TORQUE_ACCESSORS(holder, name, torque_name, type)      \
  inline type holder::name() const {                                  \
    return TorqueGeneratedClass::torque_name();                       \
  }                                                                   \
  inline type holder::name(PtrComprCageBase cage_base) const {        \
    return TorqueGeneratedClass::torque_name(cage_base);              \
  }                                                                   \
  inline void holder::set_##name(type value, WriteBarrierMode mode) { \
    TorqueGeneratedClass::set_##torque_name(value, mode);             \
  }

#define RENAME_PRIMITIVE_TORQUE_ACCESSORS(holder, name, torque_name, type)  \
  type holder::name() const { return TorqueGeneratedClass::torque_name(); } \
  void holder::set_##name(type value) {                                     \
    TorqueGeneratedClass::set_##torque_name(value);                         \
  }

#define ACCESSORS_RELAXED_CHECKED2(holder, name, type, offset, get_condition, \
                                   set_condition)                             \
  type holder::name() const {                                                 \
    PtrComprCageBase cage_base = GetPtrComprCageBase(*this);                  \
    return holder::name(cage_base);                                           \
  }                                                                           \
  type holder::name(PtrComprCageBase cage_base) const {                       \
    type value = TaggedField<type, offset>::Relaxed_Load(cage_base, *this);   \
    DCHECK(get_condition);                                                    \
    return value;                                                             \
  }                                                                           \
  void holder::set_##name(type value, WriteBarrierMode mode) {                \
    DCHECK(set_condition);                                                    \
    TaggedField<type, offset>::Relaxed_Store(*this, value);                   \
    CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);                    \
  }

#define ACCESSORS_RELAXED_CHECKED(holder, name, type, offset, condition) \
  ACCESSORS_RELAXED_CHECKED2(holder, name, type, offset, condition, condition)

#define ACCESSORS_RELAXED(holder, name, type, offset) \
  ACCESSORS_RELAXED_CHECKED(holder, name, type, offset, true)

// Similar to ACCESSORS_RELAXED above but with respective relaxed tags.
#define RELAXED_ACCESSORS_CHECKED2(holder, name, type, offset, get_condition, \
                                   set_condition)                             \
  DEF_RELAXED_GETTER(holder, name, type) {                                    \
    type value = TaggedField<type, offset>::Relaxed_Load(cage_base, *this);   \
    DCHECK(get_condition);                                                    \
    return value;                                                             \
  }                                                                           \
  void holder::set_##name(type value, RelaxedStoreTag,                        \
                          WriteBarrierMode mode) {                            \
    DCHECK(set_condition);                                                    \
    TaggedField<type, offset>::Relaxed_Store(*this, value);                   \
    CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);                    \
  }

#define RELAXED_ACCESSORS_CHECKED(holder, name, type, offset, condition) \
  RELAXED_ACCESSORS_CHECKED2(holder, name, type, offset, condition, condition)

#define RELAXED_ACCESSORS(holder, name, type, offset) \
  RELAXED_ACCESSORS_CHECKED(holder, name, type, offset, true)

#define RELEASE_ACQUIRE_GETTER_CHECKED(holder, name, type, offset,          \
                                       get_condition)                       \
  DEF_ACQUIRE_GETTER(holder, name, type) {                                  \
    type value = TaggedField<type, offset>::Acquire_Load(cage_base, *this); \
    DCHECK(get_condition);                                                  \
    return value;                                                           \
  }

#define RELEASE_ACQUIRE_SETTER_CHECKED(holder, name, type, offset, \
                                       set_condition)              \
  void holder::set_##name(type value, ReleaseStoreTag,             \
                          WriteBarrierMode mode) {                 \
    DCHECK(set_condition);                                         \
    TaggedField<type, offset>::Release_Store(*this, value);        \
    CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);         \
  }

#define RELEASE_ACQUIRE_ACCESSORS_CHECKED2(holder, name, type, offset,      \
                                           get_condition, set_condition)    \
  RELEASE_ACQUIRE_GETTER_CHECKED(holder, name, type, offset, get_condition) \
  RELEASE_ACQUIRE_SETTER_CHECKED(holder, name, type, offset, set_condition)

#define RELEASE_ACQUIRE_ACCESSORS_CHECKED(holder, name, type, offset,       \
                                          condition)                        \
  RELEASE_ACQUIRE_ACCESSORS_CHECKED2(holder, name, type, offset, condition, \
                                     condition)

#define RELEASE_ACQUIRE_ACCESSORS(holder, name, type, offset) \
  RELEASE_ACQUIRE_ACCESSORS_CHECKED(holder, name, type, offset, true)

#define WEAK_ACCESSORS_CHECKED2(holder, name, offset, get_condition,          \
                                set_condition)                                \
  DEF_GETTER(holder, name, Tagged<MaybeObject>) {                             \
    Tagged<MaybeObject> value =                                               \
        TaggedField<MaybeObject, offset>::load(cage_base, *this);             \
    DCHECK(get_condition);                                                    \
    return value;                                                             \
  }                                                                           \
  void holder::set_##name(Tagged<MaybeObject> value, WriteBarrierMode mode) { \
    DCHECK(set_condition);                                                    \
    TaggedField<MaybeObject, offset>::store(*this, value);                    \
    CONDITIONAL_WEAK_WRITE_BARRIER(*this, offset, value, mode);               \
  }

#define WEAK_ACCESSORS_CHECKED(holder, name, offset, condition) \
  WEAK_ACCESSORS_CHECKED2(holder, name, offset, condition, condition)

#define WEAK_ACCESSORS(holder, name, offset) \
  WEAK_ACCESSORS_CHECKED(holder, name, offset, true)

#define RELEASE_ACQUIRE_WEAK_ACCESSORS_CHECKED2(holder, name, offset,         \
                                                get_condition, set_condition) \
  DEF_ACQUIRE_GETTER(holder, name, Tagged<MaybeObject>) {                     \
    Tagged<MaybeObject> value =                                               \
        TaggedField<MaybeObject, offset>::Acquire_Load(cage_base, *this);     \
    DCHECK(get_condition);                                                    \
    return value;                                                             \
  }                                                                           \
  void holder::set_##name(Tagged<MaybeObject> value, ReleaseStoreTag,         \
                          WriteBarrierMode mode) {                            \
    DCHECK(set_condition);                                                    \
    TaggedField<MaybeObject, offset>::Release_Store(*this, value);            \
    CONDITIONAL_WEAK_WRITE_BARRIER(*this, offset, value, mode);               \
  }

#define RELEASE_ACQUIRE_WEAK_ACCESSORS_CHECKED(holder, name, offset,       \
                                               condition)                  \
  RELEASE_ACQUIRE_WEAK_ACCESSORS_CHECKED2(holder, name, offset, condition, \
                                          condition)

#define RELEASE_ACQUIRE_WEAK_ACCESSORS(holder, name, offset) \
  RELEASE_ACQUIRE_WEAK_ACCESSORS_CHECKED(holder, name, offset, true)

// Getter that returns a Smi as an int and writes an int as a Smi.
#define SMI_ACCESSORS_CHECKED(holder, name, offset, condition)   \
  int holder::name() const {                                     \
    DCHECK(condition);                                           \
    Tagged<Smi> value = TaggedField<Smi, offset>::load(*this);   \
    return value.value();                                        \
  }                                                              \
  void holder::set_##name(int value) {                           \
    DCHECK(condition);                                           \
    TaggedField<Smi, offset>::store(*this, Smi::FromInt(value)); \
  }

#define SMI_ACCESSORS(holder, name, offset) \
  SMI_ACCESSORS_CHECKED(holder, name, offset, true)

#define DECL_RELEASE_ACQUIRE_INT_ACCESSORS(name) \
  inline int name(AcquireLoadTag) const;         \
  inline void set_##name(int value, ReleaseStoreTag);

#define RELEASE_ACQUIRE_SMI_ACCESSORS(holder, name, offset)              \
  int holder::name(AcquireLoadTag) const {                               \
    Tagged<Smi> value = TaggedField<Smi, offset>::Acquire_Load(*this);   \
    return value.value();                                                \
  }                                                                      \
  void holder::set_##name(int value, ReleaseStoreTag) {                  \
    TaggedField<Smi, offset>::Release_Store(*this, Smi::FromInt(value)); \
  }

#define DECL_RELAXED_INT_ACCESSORS(name) \
  inline int name(RelaxedLoadTag) const; \
  inline void set_##name(int value, RelaxedStoreTag);

#define RELAXED_SMI_ACCESSORS(holder, name, offset)                      \
  int holder::name(RelaxedLoadTag) const {                               \
    Tagged<Smi> value = TaggedField<Smi, offset>::Relaxed_Load(*this);   \
    return value.value();                                                \
  }                                                                      \
  void holder::set_##name(int value, RelaxedStoreTag) {                  \
    TaggedField<Smi, offset>::Relaxed_Store(*this, Smi::FromInt(value)); \
  }

#define BOOL_GETTER(holder, field, name, offset) \
  bool holder::name() const { return BooleanBit::get(field(), offset); }

#define BOOL_ACCESSORS(holder, field, name, offset)                      \
  bool holder::name() const { return BooleanBit::get(field(), offset); } \
  void holder::set_##name(bool value) {                                  \
    set_##field(BooleanBit::set(field(), offset, value));                \
  }

#define DECL_RELAXED_BOOL_ACCESSORS(name) \
  inline bool name(RelaxedLoadTag) const; \
  inline void set_##name(bool value, RelaxedStoreTag);

#define RELAXED_BOOL_ACCESSORS(holder, field, name, offset)          \
  bool holder::name(RelaxedLoadTag) const {                          \
    return BooleanBit::get(field(kRelaxedLoad), offset);             \
  }                                                                  \
  void holder::set_##name(bool value, RelaxedStoreTag) {             \
    set_##field(BooleanBit::set(field(kRelaxedLoad), offset, value), \
                kRelaxedStore);                                      \
  }

// Host objects in ReadOnlySpace can't define the isolate-less accessor.
#define DECL_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST(name, type) \
  inline type name(i::IsolateForSandbox isolate) const;                  \
  inline void init_##name(i::IsolateForSandbox isolate,                  \
                          const type initial_value);                     \
  inline void set_##name(i::IsolateForSandbox isolate, const type value);

// Host objects in ReadOnlySpace can't define the isolate-less accessor.
#define EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST(holder, name, type, \
                                                        offset, tag)        \
  type holder::name(i::IsolateForSandbox isolate) const {                   \
    /* This is a workaround for MSVC error C2440 not allowing  */           \
    /* reinterpret casts to the same type. */                               \
    struct C2440 {};                                                        \
    Address result =                                                        \
        HeapObject::ReadExternalPointerField<tag>(offset, isolate);         \
    return reinterpret_cast<type>(reinterpret_cast<C2440*>(result));        \
  }                                                                         \
  void holder::init_##name(i::IsolateForSandbox isolate,                    \
                           const type initial_value) {                      \
    /* This is a workaround for MSVC error C2440 not allowing  */           \
    /* reinterpret casts to the same type. */                               \
    struct C2440 {};                                                        \
    Address the_value = reinterpret_cast<Address>(                          \
        reinterpret_cast<const C2440*>(initial_value));                     \
    HeapObject::InitExternalPointerField<tag>(offset, isolate, the_value);  \
  }                                                                         \
  void holder::set_##name(i::IsolateForSandbox isolate, const type value) { \
    /* This is a workaround for MSVC error C2440 not allowing  */           \
    /* reinterpret casts to the same type. */                               \
    struct C2440 {};                                                        \
    Address the_value =                                                     \
        reinterpret_cast<Address>(reinterpret_cast<const C2440*>(value));   \
    HeapObject::WriteExternalPointerField<tag>(offset, isolate, the_value); \
  }

#define DECL_EXTERNAL_POINTER_ACCESSORS(name, type) \
  inline type name() const;                         \
  DECL_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST(name, type)

#define EXTERNAL_POINTER_ACCESSORS(holder, name, type, offset, tag)           \
  type holder::name() const {                                                 \
    i::IsolateForSandbox isolate = GetIsolateForSandbox(*this);               \
    return holder::name(isolate);                                             \
  }                                                                           \
  EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST(holder, name, type, offset, \
                                                  tag)

#define DECL_TRUSTED_POINTER_ACCESSORS(name, type)                           \
  /* Trusted pointers currently always have release-acquire semantics. */    \
  /* However, we still expose explicit release-acquire accessors so it */    \
  /* can be made clear when they are required. */                            \
  /* If desired, we could create separate {Read|Write}TrustedPointer */      \
  /* routines for relaxed- and release-acquire semantics in the future. */   \
  inline Tagged<type> name(IsolateForSandbox isolate) const;                 \
  inline Tagged<type> name(IsolateForSandbox isolate, AcquireLoadTag) const; \
  inline void set_##name(Tagged<type> value,                                 \
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);      \
  inline void set_##name(Tagged<type> value, ReleaseStoreTag,                \
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);      \
  inline bool has_##name() const;                                            \
  inline void clear_##name();

#define TRUSTED_POINTER_ACCESSORS(holder, name, type, offset, tag)             \
  Tagged<type> holder::name(IsolateForSandbox isolate) const {                 \
    return name(isolate, kAcquireLoad);                                        \
  }                                                                            \
  Tagged<type> holder::name(IsolateForSandbox isolate, AcquireLoadTag) const { \
    DCHECK(has_##name());                                                      \
    return type::cast(ReadTrustedPointerField<tag>(offset, isolate));          \
  }                                                                            \
  void holder::set_##name(Tagged<type> value, WriteBarrierMode mode) {         \
    set_##name(value, kReleaseStore, mode);                                    \
  }                                                                            \
  void holder::set_##name(Tagged<type> value, ReleaseStoreTag,                 \
                          WriteBarrierMode mode) {                             \
    WriteTrustedPointerField<tag>(offset, value);                              \
    CONDITIONAL_TRUSTED_POINTER_WRITE_BARRIER(*this, offset, tag, value,       \
                                              mode);                           \
  }                                                                            \
  bool holder::has_##name() const {                                            \
    return !IsTrustedPointerFieldCleared(offset);                              \
  }                                                                            \
  void holder::clear_##name() { ClearTrustedPointerField(offset); }

#define DECL_CODE_POINTER_ACCESSORS(name) \
  DECL_TRUSTED_POINTER_ACCESSORS(name, Code)
#define CODE_POINTER_ACCESSORS(holder, name, offset) \
  TRUSTED_POINTER_ACCESSORS(holder, name, Code, offset, kCodeIndirectPointerTag)

// Accessors for "protected" pointers, i.e. references from one trusted object
// to another trusted object. For these pointers it can be assumed that neither
// the pointer nor the pointed-to object can be manipulated by an attacker.
#define DECL_PROTECTED_POINTER_ACCESSORS(name, type)                    \
  inline Tagged<type> name() const;                                     \
  inline void set_##name(Tagged<type> value,                            \
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER); \
  inline bool has_##name() const;                                       \
  inline void clear_##name();

#define PROTECTED_POINTER_ACCESSORS(holder, name, type, offset)              \
  static_assert(std::is_base_of<TrustedObject, holder>::value);              \
  Tagged<type> holder::name() const {                                        \
    DCHECK(has_##name());                                                    \
    return type::cast(ReadProtectedPointerField(offset));                    \
  }                                                                          \
  void holder::set_##name(Tagged<type> value, WriteBarrierMode mode) {       \
    WriteProtectedPointerField(offset, value);                               \
    CONDITIONAL_PROTECTED_POINTER_WRITE_BARRIER(*this, offset, value, mode); \
  }                                                                          \
  bool holder::has_##name() const {                                          \
    return !IsProtectedPointerFieldCleared(offset);                          \
  }                                                                          \
  void holder::clear_##name() { return ClearProtectedPointerField(offset); }

#define DECL_RELEASE_ACQUIRE_PROTECTED_POINTER_ACCESSORS(name, type)    \
  inline Tagged<type> name(AcquireLoadTag) const;                       \
  inline void set_##name(Tagged<type> value, ReleaseStoreTag,           \
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER); \
  inline bool has_##name(AcquireLoadTag) const;                         \
  inline void clear_##name(ReleaseStoreTag);

#define RELEASE_ACQUIRE_PROTECTED_POINTER_ACCESSORS(holder, name, type,      \
                                                    offset)                  \
  static_assert(std::is_base_of<TrustedObject, holder>::value);              \
  Tagged<type> holder::name(AcquireLoadTag tag) const {                      \
    DCHECK(has_##name(tag));                                                 \
    return type::cast(ReadProtectedPointerField(offset, tag));               \
  }                                                                          \
  void holder::set_##name(Tagged<type> value, ReleaseStoreTag tag,           \
                          WriteBarrierMode mode) {                           \
    WriteProtectedPointerField(offset, value, tag);                          \
    CONDITIONAL_PROTECTED_POINTER_WRITE_BARRIER(*this, offset, value, mode); \
  }                                                                          \
  bool holder::has_##name(AcquireLoadTag tag) const {                        \
    return !IsProtectedPointerFieldCleared(offset, tag);                     \
  }                                                                          \
  void holder::clear_##name(ReleaseStoreTag tag) {                           \
    return ClearProtectedPointerField(offset, tag);                          \
  }

#define BIT_FIELD_ACCESSORS2(holder, get_field, set_field, name, BitField) \
  typename BitField::FieldType holder::name() const {                      \
    return BitField::decode(get_field());                                  \
  }                                                                        \
  void holder::set_##name(typename BitField::FieldType value) {            \
    set_##set_field(BitField::update(set_field(), value));                 \
  }

#define BIT_FIELD_ACCESSORS(holder, field, name, BitField) \
  BIT_FIELD_ACCESSORS2(holder, field, field, name, BitField)

#define RELAXED_INT16_ACCESSORS(holder, name, offset) \
  int16_t holder::name() const {                      \
    return RELAXED_READ_INT16_FIELD(*this, offset);   \
  }                                                   \
  void holder::set_##name(int16_t value) {            \
    RELAXED_WRITE_INT16_FIELD(*this, offset, value);  \
  }

#define FIELD_ADDR(p, offset) ((p).ptr() + offset - kHeapObjectTag)

#define SEQ_CST_READ_FIELD(p, offset) \
  TaggedField<Object>::SeqCst_Load(p, offset)

#define ACQUIRE_READ_FIELD(p, offset) \
  TaggedField<Object>::Acquire_Load(p, offset)

#define RELAXED_READ_FIELD(p, offset) \
  TaggedField<Object>::Relaxed_Load(p, offset)

#define RELAXED_READ_WEAK_FIELD(p, offset) \
  TaggedField<MaybeObject>::Relaxed_Load(p, offset)

#define WRITE_FIELD(p, offset, value) \
  TaggedField<Object>::store(p, offset, value)

#define SEQ_CST_WRITE_FIELD(p, offset, value) \
  TaggedField<Object>::SeqCst_Store(p, offset, value)

#define RELEASE_WRITE_FIELD(p, offset, value) \
  TaggedField<Object>::Release_Store(p, offset, value)

#define RELAXED_WRITE_FIELD(p, offset, value) \
  TaggedField<Object>::Relaxed_Store(p, offset, value)

#define RELAXED_WRITE_WEAK_FIELD(p, offset, value) \
  TaggedField<MaybeObject>::Relaxed_Store(p, offset, value)

#define SEQ_CST_SWAP_FIELD(p, offset, value) \
  TaggedField<Object>::SeqCst_Swap(p, offset, value)

#define SEQ_CST_COMPARE_AND_SWAP_FIELD(p, offset, expected, value) \
  TaggedField<Object>::SeqCst_CompareAndSwap(p, offset, expected, value)

#ifdef V8_DISABLE_WRITE_BARRIERS
#define WRITE_BARRIER(object, offset, value)
#define WRITE_BARRIER_CPP(object, offset, value)
#else
#define WRITE_BARRIER(object, offset, value)                        \
  do {                                                              \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));             \
    static_assert(kTaggedCanConvertToRawObjects);                   \
    CombinedWriteBarrier(object, (object)->RawField(offset), value, \
                         UPDATE_WRITE_BARRIER);                     \
  } while (false)
#endif

#ifdef V8_DISABLE_WRITE_BARRIERS
#define WEAK_WRITE_BARRIER(object, offset, value)
#else
#define WEAK_WRITE_BARRIER(object, offset, value)                            \
  do {                                                                       \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));                      \
    static_assert(kTaggedCanConvertToRawObjects);                            \
    CombinedWriteBarrier(object, (object)->RawMaybeWeakField(offset), value, \
                         UPDATE_WRITE_BARRIER);                              \
  } while (false)
#endif

#ifdef V8_DISABLE_WRITE_BARRIERS
#define EPHEMERON_KEY_WRITE_BARRIER(object, offset, value)
#elif V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS
#define EPHEMERON_KEY_WRITE_BARRIER(object, offset, value) \
  WRITE_BARRIER(object, offset, value)
#else
#define EPHEMERON_KEY_WRITE_BARRIER(object, offset, value)           \
  do {                                                               \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));              \
    CombinedEphemeronWriteBarrier(EphemeronHashTable::cast(object),  \
                                  (object)->RawField(offset), value, \
                                  UPDATE_WRITE_BARRIER);             \
  } while (false)
#endif

#ifdef V8_DISABLE_WRITE_BARRIERS
#define INDIRECT_POINTER_WRITE_BARRIER(object, offset, tag, value)
#else
#define INDIRECT_POINTER_WRITE_BARRIER(object, offset, tag, value)           \
  do {                                                                       \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));                      \
    IndirectPointerWriteBarrier(                                             \
        object, Tagged(object)->RawIndirectPointerField(offset, tag), value, \
        UPDATE_WRITE_BARRIER);                                               \
  } while (false)
#endif

#ifdef V8_DISABLE_WRITE_BARRIERS
#define CONDITIONAL_WRITE_BARRIER(object, offset, value, mode)
#elif V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS
#define CONDITIONAL_WRITE_BARRIER(object, offset, value, mode) \
  WRITE_BARRIER(object, offset, value)
#else
#define CONDITIONAL_WRITE_BARRIER(object, offset, value, mode)             \
  do {                                                                     \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));                    \
    CombinedWriteBarrier(object, (object)->RawField(offset), value, mode); \
  } while (false)
#endif

#ifdef V8_DISABLE_WRITE_BARRIERS
#define CONDITIONAL_WEAK_WRITE_BARRIER(object, offset, value, mode)
#elif V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS
#define CONDITIONAL_WEAK_WRITE_BARRIER(object, offset, value, mode) \
  WRITE_BARRIER(object, offset, value)
#else
#define CONDITIONAL_WEAK_WRITE_BARRIER(object, offset, value, mode)          \
  do {                                                                       \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));                      \
    CombinedWriteBarrier(object, (object)->RawMaybeWeakField(offset), value, \
                         mode);                                              \
  } while (false)
#endif

#ifdef V8_DISABLE_WRITE_BARRIERS
#define CONDITIONAL_EPHEMERON_KEY_WRITE_BARRIER(object, offset, value, mode)
#else
#define CONDITIONAL_EPHEMERON_KEY_WRITE_BARRIER(object, offset, value, mode) \
  do {                                                                       \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));                      \
    CombinedEphemeronWriteBarrier(EphemeronHashTable::cast(object),          \
                                  (object)->RawField(offset), value, mode);  \
  } while (false)
#endif

#ifdef V8_DISABLE_WRITE_BARRIERS
#define CONDITIONAL_INDIRECT_POINTER_WRITE_BARRIER(object, offset, tag, value, \
                                                   mode)
#else
#define CONDITIONAL_INDIRECT_POINTER_WRITE_BARRIER(object, offset, tag, value, \
                                                   mode)                       \
  do {                                                                         \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));                        \
    IndirectPointerWriteBarrier(                                               \
        object, (object).RawIndirectPointerField(offset, tag), value, mode);   \
  } while (false)
#endif

#ifdef V8_ENABLE_SANDBOX
#define CONDITIONAL_TRUSTED_POINTER_WRITE_BARRIER(object, offset, tag, value, \
                                                  mode)                       \
  CONDITIONAL_INDIRECT_POINTER_WRITE_BARRIER(object, offset, tag, value, mode)
#else
#define CONDITIONAL_TRUSTED_POINTER_WRITE_BARRIER(object, offset, tag, value, \
                                                  mode)                       \
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
#endif
#define CONDITIONAL_CODE_POINTER_WRITE_BARRIER(object, offset, value, mode) \
  CONDITIONAL_TRUSTED_POINTER_WRITE_BARRIER(                                \
      object, offset, kCodeIndirectPointerTag, value, mode)

#define CONDITIONAL_PROTECTED_POINTER_WRITE_BARRIER(object, offset, value, \
                                                    mode)                  \
  do {                                                                     \
    DCHECK_NOT_NULL(GetHeapFromWritableObject(object));                    \
    ProtectedPointerWriteBarrier(                                          \
        object, (object).RawProtectedPointerField(offset), value, mode);   \
  } while (false)

#define ACQUIRE_READ_INT8_FIELD(p, offset) \
  static_cast<int8_t>(base::Acquire_Load(  \
      reinterpret_cast<const base::Atomic8*>(FIELD_ADDR(p, offset))))

#define ACQUIRE_READ_INT32_FIELD(p, offset) \
  static_cast<int32_t>(base::Acquire_Load(  \
      reinterpret_cast<const base::Atomic32*>(FIELD_ADDR(p, offset))))

#define RELAXED_WRITE_INT8_FIELD(p, offset, value)                             \
  base::Relaxed_Store(reinterpret_cast<base::Atomic8*>(FIELD_ADDR(p, offset)), \
                      static_cast<base::Atomic8>(value));
#define RELAXED_READ_INT8_FIELD(p, offset) \
  static_cast<int8_t>(base::Relaxed_Load(  \
      reinterpret_cast<const base::Atomic8*>(FIELD_ADDR(p, offset))))

#define RELAXED_READ_UINT16_FIELD(p, offset) \
  static_cast<uint16_t>(base::Relaxed_Load(  \
      reinterpret_cast<const base::Atomic16*>(FIELD_ADDR(p, offset))))

#define RELAXED_WRITE_UINT16_FIELD(p, offset, value)            \
  base::Relaxed_Store(                                          \
      reinterpret_cast<base::Atomic16*>(FIELD_ADDR(p, offset)), \
      static_cast<base::Atomic16>(value));

#define RELAXED_READ_INT16_FIELD(p, offset) \
  static_cast<int16_t>(base::Relaxed_Load(  \
      reinterpret_cast<const base::Atomic16*>(FIELD_ADDR(p, offset))))

#define RELAXED_WRITE_INT16_FIELD(p, offset, value)             \
  base::Relaxed_Store(                                          \
      reinterpret_cast<base::Atomic16*>(FIELD_ADDR(p, offset)), \
      static_cast<base::Atomic16>(value));

#define RELAXED_READ_UINT32_FIELD(p, offset) \
  static_cast<uint32_t>(base::Relaxed_Load(  \
      reinterpret_cast<const base::Atomic32*>(FIELD_ADDR(p, offset))))

#define ACQUIRE_READ_UINT32_FIELD(p, offset) \
  static_cast<uint32_t>(base::Acquire_Load(  \
      reinterpret_cast<const base::Atomic32*>(FIELD_ADDR(p, offset))))

#define RELAXED_WRITE_UINT32_FIELD(p, offset, value)            \
  base::Relaxed_Store(                                          \
      reinterpret_cast<base::Atomic32*>(FIELD_ADDR(p, offset)), \
      static_cast<base::Atomic32>(value));

#define RELEASE_WRITE_INT8_FIELD(p, offset, value)                             \
  base::Release_Store(reinterpret_cast<base::Atomic8*>(FIELD_ADDR(p, offset)), \
                      static_cast<base::Atomic8>(value));

#define RELEASE_WRITE_UINT32_FIELD(p, offset, value)            \
  base::Release_Store(                                          \
      reinterpret_cast<base::Atomic32*>(FIELD_ADDR(p, offset)), \
      static_cast<base::Atomic32>(value));

#define RELAXED_READ_INT32_FIELD(p, offset) \
  static_cast<int32_t>(base::Relaxed_Load(  \
      reinterpret_cast<const base::Atomic32*>(FIELD_ADDR(p, offset))))

#if defined(V8_HOST_ARCH_64_BIT)
#define RELAXED_READ_INT64_FIELD(p, offset) \
  static_cast<int64_t>(base::Relaxed_Load(  \
      reinterpret_cast<const base::Atomic64*>(FIELD_ADDR(p, offset))))
#endif

#define RELEASE_WRITE_INT32_FIELD(p, offset, value)             \
  base::Release_Store(                                          \
      reinterpret_cast<base::Atomic32*>(FIELD_ADDR(p, offset)), \
      static_cast<base::Atomic32>(value))

#define RELAXED_WRITE_INT32_FIELD(p, offset, value)             \
  base::Relaxed_Store(                                          \
      reinterpret_cast<base::Atomic32*>(FIELD_ADDR(p, offset)), \
      static_cast<base::Atomic32>(value))

static_assert(sizeof(int) == sizeof(int32_t),
              "sizeof int must match sizeof int32_t");

#define RELAXED_READ_INT_FIELD(p, offset) RELAXED_READ_INT32_FIELD(p, offset)

#define RELAXED_WRITE_INT_FIELD(p, offset, value) \
  RELAXED_WRITE_INT32_FIELD(p, offset, value)

static_assert(sizeof(unsigned) == sizeof(uint32_t),
              "sizeof unsigned must match sizeof uint32_t");

#define RELAXED_READ_UINT_FIELD(p, offset) RELAXED_READ_UINT32_FIELD(p, offset)

#define RELAXED_WRITE_UINT_FIELD(p, offset, value) \
  RELAXED_WRITE_UINT32_FIELD(p, offset, value)

#define RELAXED_READ_BYTE_FIELD(p, offset) \
  static_cast<uint8_t>(base::Relaxed_Load( \
      reinterpret_cast<const base::Atomic8*>(FIELD_ADDR(p, offset))))

#define ACQUIRE_READ_BYTE_FIELD(p, offset) \
  static_cast<uint8_t>(base::Acquire_Load( \
      reinterpret_cast<const base::Atomic8*>(FIELD_ADDR(p, offset))))

#define RELAXED_WRITE_BYTE_FIELD(p, offset, value)                             \
  base::Relaxed_Store(reinterpret_cast<base::Atomic8*>(FIELD_ADDR(p, offset)), \
                      static_cast<base::Atomic8>(value));

#define RELEASE_WRITE_BYTE_FIELD(p, offset, value)                             \
  base::Release_Store(reinterpret_cast<base::Atomic8*>(FIELD_ADDR(p, offset)), \
                      static_cast<base::Atomic8>(value));

#ifdef OBJECT_PRINT
#define DECL_PRINTER(Name) void Name##Print(std::ostream& os);
#else
#define DECL_PRINTER(Name)
#endif

#ifdef VERIFY_HEAP
#define DECL_VERIFIER(Name) void Name##Verify(Isolate* isolate);
#define EXPORT_DECL_VERIFIER(Name) \
  V8_EXPORT_PRIVATE void Name##Verify(Isolate* isolate);
#define DECL_STATIC_VERIFIER(Name) \
  static void Name##Verify(Tagged<Name> obj, Isolate* isolate);
#define EXPORT_DECL_STATIC_VERIFIER(Name)                      \
  V8_EXPORT_PRIVATE static void Name##Verify(Tagged<Name> obj, \
                                             Isolate* isolate);
#else
#define DECL_VERIFIER(Name)
#define EXPORT_DECL_VERIFIER(Name)
#define DECL_STATIC_VERIFIER(Name)
#define EXPORT_DECL_STATIC_VERIFIER(Name)
#endif

#define DEFINE_DEOPT_ELEMENT_ACCESSORS(name, type) \
  type DeoptimizationData::name() const {          \
    return type::cast(get(k##name##Index));        \
  }                                                \
  void DeoptimizationData::Set##name(type value) { set(k##name##Index, value); }

#define DEFINE_DEOPT_ENTRY_ACCESSORS(name, type)                \
  type DeoptimizationData::name(int i) const {                  \
    return type::cast(get(IndexForEntry(i) + k##name##Offset)); \
  }                                                             \
  void DeoptimizationData::Set##name(int i, type value) {       \
    set(IndexForEntry(i) + k##name##Offset, value);             \
  }

#define TQ_OBJECT_CONSTRUCTORS(Type)                             \
  OBJECT_CONSTRUCTORS(Type, TorqueGenerated##Type<Type, Super>); \
  friend class TorqueGenerated##Type<Type, Super>;

#define TQ_OBJECT_CONSTRUCTORS_IMPL(Type) \
  inline Type::Type(Address ptr)          \
      : TorqueGenerated##Type<Type, Type::Super>(ptr) {}

#define TQ_CPP_OBJECT_DEFINITION_ASSERTS(_class, parent) \
  template class TorqueGenerated##_class##Asserts<_class, parent>;

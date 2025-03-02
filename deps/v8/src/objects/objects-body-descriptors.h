// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_H_
#define V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_H_

#include "src/objects/map.h"
#include "src/objects/objects.h"

namespace v8::internal {

// This is the base class for object's body descriptors.
//
// Each BodyDescriptor subclass must provide the following methods:
//
// 1) Iterate object's body using stateful object visitor.
//
//   template <typename ObjectVisitor>
//   static inline void IterateBody(Tagged<Map> map, HeapObject obj, int
//   object_size,
//                                  ObjectVisitor* v);
class BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IteratePointers(Tagged<HeapObject> obj, int start_offset,
                                     int end_offset, ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IteratePointer(Tagged<HeapObject> obj, int offset,
                                    ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IterateCustomWeakPointers(Tagged<HeapObject> obj,
                                               int start_offset, int end_offset,
                                               ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IterateCustomWeakPointer(Tagged<HeapObject> obj,
                                              int offset, ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IterateEphemeron(Tagged<HeapObject> obj, int index,
                                      int key_offset, int value_offset,
                                      ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IterateMaybeWeakPointers(Tagged<HeapObject> obj,
                                              int start_offset, int end_offset,
                                              ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IterateMaybeWeakPointer(Tagged<HeapObject> obj, int offset,
                                             ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IterateTrustedPointer(Tagged<HeapObject> obj, int offset,
                                           ObjectVisitor* visitor,
                                           IndirectPointerMode mode,
                                           IndirectPointerTag tag);
  template <typename ObjectVisitor>
  static inline void IterateCodePointer(Tagged<HeapObject> obj, int offset,
                                        ObjectVisitor* visitor,
                                        IndirectPointerMode mode);
  template <typename ObjectVisitor>
  static inline void IterateSelfIndirectPointer(Tagged<HeapObject> obj,
                                                IndirectPointerTag tag,
                                                ObjectVisitor* v);

  template <typename ObjectVisitor>
  static inline void IterateProtectedPointer(Tagged<HeapObject> obj, int offset,
                                             ObjectVisitor* v);
#ifdef V8_ENABLE_LEAPTIERING
  template <typename ObjectVisitor>
  static inline void IterateJSDispatchEntry(Tagged<HeapObject> obj, int offset,
                                            ObjectVisitor* v);
#endif  // V8_ENABLE_LEAPTIERING

 protected:
  // Returns true for all header and embedder fields.
  static inline bool IsValidEmbedderJSObjectSlotImpl(Tagged<Map> map,
                                                     Tagged<HeapObject> obj,
                                                     int offset);

  // Treats all header and in-object fields in the range as tagged. Figures out
  // dynamically whether the object has embedder fields and visits them
  // accordingly (as tagged fields and as external pointers).
  template <typename ObjectVisitor>
  static inline void IterateJSObjectBodyImpl(Tagged<Map> map,
                                             Tagged<HeapObject> obj,
                                             int start_offset, int end_offset,
                                             ObjectVisitor* v);

  // Treats all header and in-object fields in the range as tagged.
  template <typename ObjectVisitor>
  static inline void IterateJSObjectBodyWithoutEmbedderFieldsImpl(
      Tagged<Map> map, Tagged<HeapObject> obj, int start_offset, int end_offset,
      ObjectVisitor* v);
};

// This class describes a body of an object without any pointers.
class DataOnlyBodyDescriptor : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {}

 private:
  // Note: {SizeOf} is not implemented here; sub-classes will have to implement
  // it.
};

// This class describes a body of an object in which all pointer fields are
// located in the [start_offset, end_offset) interval.
// All pointers have to be strong.
template <int start_offset, int end_offset>
class FixedRangeBodyDescriptor : public BodyDescriptorBase {
 public:
  static const int kStartOffset = start_offset;
  static const int kEndOffset = end_offset;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 ObjectVisitor* v) {
    IteratePointers(obj, start_offset, end_offset, v);
  }

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateBody(map, obj, v);
  }

  // Note: {SizeOf} is not implemented here; sub-classes will have to implement
  // it.
};

// This class describes a body of an object of a fixed size
// in which all pointer fields are located in the [start_offset, end_offset)
// interval.
// All pointers have to be strong.
template <int start_offset, int end_offset, int size>
class FixedBodyDescriptor
    : public std::conditional_t<
          start_offset == end_offset, DataOnlyBodyDescriptor,
          FixedRangeBodyDescriptor<start_offset, end_offset>> {
 public:
  static constexpr int kSize = size;

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    DCHECK_EQ(kSize, map->instance_size());
    return kSize;
  }
};

template <typename T>
using FixedBodyDescriptorFor =
    FixedBodyDescriptor<T::kStartOfStrongFieldsOffset,
                        T::kEndOfStrongFieldsOffset, T::kSize>;

// This class describes a body of an object in which all pointer fields are
// located in the [start_offset, object_size) interval.
// All pointers have to be strong.
template <int start_offset>
class SuffixRangeBodyDescriptor : public BodyDescriptorBase {
 public:
  static const int kStartOffset = start_offset;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, start_offset, object_size, v);
  }

  // Note: {SizeOf} is not implemented here; sub-classes will have to implement
  // it.
};

// This class describes a body of an object of a variable size
// in which all pointer fields are located in the [start_offset, object_size)
// interval.
// All pointers have to be strong.
template <int start_offset>
class FlexibleBodyDescriptor : public SuffixRangeBodyDescriptor<start_offset> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object);
};

// A forward-declarable descriptor body alias for most of the Struct successors.
class StructBodyDescriptor
    : public FlexibleBodyDescriptor<HeapObject::kHeaderSize> {};

// This class describes a body of an object in which all pointer fields are
// located in the [start_offset, object_size) interval.
// Pointers may be strong or may be Tagged<MaybeObject>-style weak pointers.
template <int start_offset>
class SuffixRangeWeakBodyDescriptor : public BodyDescriptorBase {
 public:
  static const int kStartOffset = start_offset;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateMaybeWeakPointers(obj, start_offset, object_size, v);
  }

  // Note: {SizeOf} is not implemented here; sub-classes will have to implement
  // it.
};

// This class describes a body of an object of a variable size
// in which all pointer fields are located in the [start_offset, object_size)
// interval.
// Pointers may be strong or may be Tagged<MaybeObject>-style weak pointers.
template <int start_offset>
class FlexibleWeakBodyDescriptor
    : public SuffixRangeWeakBodyDescriptor<start_offset> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object);
};

// This class describes a body of an object which has a parent class that also
// has a body descriptor. This represents a union of the parent's body
// descriptor, and a new descriptor for the child -- so, both parent and child's
// slots are iterated. The parent must be fixed size, and its slots be disjoint
// with the child's.
template <class ParentBodyDescriptor, class ChildBodyDescriptor>
class SubclassBodyDescriptor : public BodyDescriptorBase {
 public:
  // The parent must end be before the child's start offset, to make sure that
  // their slots are disjoint.
  static_assert(ParentBodyDescriptor::kSize <=
                ChildBodyDescriptor::kStartOffset);

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 ObjectVisitor* v) {
    ParentBodyDescriptor::IterateBody(map, obj, v);
    ChildBodyDescriptor::IterateBody(map, obj, v);
  }

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    ParentBodyDescriptor::IterateBody(map, obj, object_size, v);
    ChildBodyDescriptor::IterateBody(map, obj, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    // The child should know its full size.
    return ChildBodyDescriptor::SizeOf(map, object);
  }
};

// Visitor for exposed trusted objects with fixed layout according to
// FixedBodyDescriptor.
template <typename T, IndirectPointerTag kTag>
class FixedExposedTrustedObjectBodyDescriptor
    : public FixedBodyDescriptorFor<T> {
  static_assert(std::is_base_of_v<ExposedTrustedObject, T>);
  using Base = FixedBodyDescriptorFor<T>;

 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Base::IterateSelfIndirectPointer(obj, kTag, v);
    Base::IterateBody(map, obj, object_size, v);
  }
};

// A mix-in for visiting a trusted pointer field.
template <size_t kFieldOffset, IndirectPointerTag kTag>
struct WithStrongTrustedPointer {
  template <typename Base>
  class BodyDescriptor : public Base {
   public:
    template <typename ObjectVisitor>
    static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                   int object_size, ObjectVisitor* v) {
      Base::IterateBody(map, obj, object_size, v);
      Base::IterateTrustedPointer(obj, kFieldOffset, v,
                                  IndirectPointerMode::kStrong, kTag);
    }
  };
};

template <size_t kFieldOffset>
using WithStrongCodePointer =
    WithStrongTrustedPointer<kFieldOffset, kCodeIndirectPointerTag>;

// A mix-in for visiting an external pointer field.
template <size_t kFieldOffset, ExternalPointerTagRange kTagRange>
struct WithExternalPointer {
  template <typename Base>
  class BodyDescriptor : public Base {
   public:
    template <typename ObjectVisitor>
    static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                   int object_size, ObjectVisitor* v) {
      Base::IterateBody(map, obj, object_size, v);
      v->VisitExternalPointer(
          obj, obj->RawExternalPointerField(kFieldOffset, kTagRange));
    }
  };
};

// A mix-in for visiting an external pointer field.
template <size_t kFieldOffset>
struct WithProtectedPointer {
  template <typename Base>
  class BodyDescriptor : public Base {
   public:
    template <typename ObjectVisitor>
    static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                   int object_size, ObjectVisitor* v) {
      Base::IterateBody(map, obj, object_size, v);
      Base::IterateProtectedPointer(obj, kFieldOffset, v);
    }
  };
};

// Stack multiple body descriptors; the first template argument is the base,
// followed by mix-ins.
template <typename Base, typename FirstMixin, typename... MoreMixins>
class StackedBodyDescriptor
    : public StackedBodyDescriptor<
          typename FirstMixin::template BodyDescriptor<Base>, MoreMixins...> {};

// Define a specialization for the base case of only one mixin.
template <typename Base, typename FirstMixin>
class StackedBodyDescriptor<Base, FirstMixin>
    : public FirstMixin::template BodyDescriptor<Base> {};

}  // namespace v8::internal

#endif  // V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_H_

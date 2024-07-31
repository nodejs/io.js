// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_H_
#define V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_H_

#include "src/objects/map.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

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

 protected:
  // Returns true for all header and embedder fields.
  static inline bool IsValidEmbedderJSObjectSlotImpl(Tagged<Map> map,
                                                     Tagged<HeapObject> obj,
                                                     int offset);

  // Treats all header and embedder fields in the range as tagged.
  template <typename ObjectVisitor>
  static inline void IterateJSObjectBodyImpl(Tagged<Map> map,
                                             Tagged<HeapObject> obj,
                                             int start_offset, int end_offset,
                                             ObjectVisitor* v);
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

 private:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    // Has to be implemented by the subclass.
    UNREACHABLE();
  }
};

// This class describes a body of an object of a fixed size
// in which all pointer fields are located in the [start_offset, end_offset)
// interval.
// All pointers have to be strong.
template <int start_offset, int end_offset, int size>
class FixedBodyDescriptor
    : public FixedRangeBodyDescriptor<start_offset, end_offset> {
 public:
  static const int kSize = size;
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kSize;
  }
};

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

 private:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    // Has to be implemented by the subclass.
    UNREACHABLE();
  }
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

// A forward-declacable descriptor body alias for most of the Struct successors.
class StructBodyDescriptor
    : public FlexibleBodyDescriptor<HeapObject::kHeaderSize> {
 public:
};

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

 private:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    // Has to be implemented by the subclass.
    UNREACHABLE();
  }
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

// This class describes a body of an object without any pointers.
class DataOnlyBodyDescriptor : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {}

 private:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    // Has to be implemented by the subclass.
    UNREACHABLE();
  }
};

// This class describes a body of an object which has a parent class that also
// has a body descriptor. This represents a union of the parent's body
// descriptor, and a new descriptor for the child -- so, both parent and child's
// slots are iterated. The parent must be fixed size, and its slots be disjoint
// with the child's.
template <class ParentBodyDescriptor, class ChildBodyDescriptor>
class SubclassBodyDescriptor final : public BodyDescriptorBase {
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

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_H_

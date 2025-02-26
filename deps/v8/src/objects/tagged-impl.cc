// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/tagged-impl.h"

#include <sstream>

#include "src/heap/heap-layout-inl.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"
#include "src/objects/tagged-impl-inl.h"
#include "src/strings/string-stream.h"
#include "src/utils/ostreams.h"

#ifdef V8_EXTERNAL_CODE_SPACE
// For IsCodeSpaceObject().
#include "src/heap/heap-write-barrier-inl.h"
#endif

namespace v8 {
namespace internal {

#if defined(V8_EXTERNAL_CODE_SPACE) || defined(V8_ENABLE_SANDBOX)
bool CheckObjectComparisonAllowed(Address a, Address b) {
  if (!HAS_STRONG_HEAP_OBJECT_TAG(a) || !HAS_STRONG_HEAP_OBJECT_TAG(b)) {
    return true;
  }
  Tagged<HeapObject> obj_a = UncheckedCast<HeapObject>(Tagged<Object>(a));
  Tagged<HeapObject> obj_b = UncheckedCast<HeapObject>(Tagged<Object>(b));
  // This check might fail when we try to compare objects in different pointer
  // compression cages (e.g. the one used by code space or trusted space) with
  // each other. The main legitimate case when such "mixed" comparison could
  // happen is comparing two AbstractCode objects. If that's the case one must
  // use AbstractCode's == operator instead of Object's one or SafeEquals().
  CHECK_EQ(HeapLayout::InCodeSpace(obj_a), HeapLayout::InCodeSpace(obj_b));
#ifdef V8_ENABLE_SANDBOX
  CHECK_EQ(HeapLayout::InTrustedSpace(obj_a),
           HeapLayout::InTrustedSpace(obj_b));
#endif
  return true;
}
#endif  // defined(V8_EXTERNAL_CODE_SPACE) || defined(V8_ENABLE_SANDBOX)

template <HeapObjectReferenceType kRefType, typename StorageType>
void ShortPrint(TaggedImpl<kRefType, StorageType> ptr, FILE* out) {
  OFStream os(out);
  os << Brief(ptr);
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void ShortPrint(
    TaggedImpl<HeapObjectReferenceType::STRONG, Address> ptr, FILE* out);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void ShortPrint(
    TaggedImpl<HeapObjectReferenceType::WEAK, Address> ptr, FILE* out);

template <HeapObjectReferenceType kRefType, typename StorageType>
void ShortPrint(TaggedImpl<kRefType, StorageType> ptr,
                StringStream* accumulator) {
  std::ostringstream os;
  os << Brief(ptr);
  accumulator->Add(os.str().c_str());
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void ShortPrint(
    TaggedImpl<HeapObjectReferenceType::STRONG, Address> ptr,
    StringStream* accumulator);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void ShortPrint(
    TaggedImpl<HeapObjectReferenceType::WEAK, Address> ptr,
    StringStream* accumulator);

template <HeapObjectReferenceType kRefType, typename StorageType>
void ShortPrint(TaggedImpl<kRefType, StorageType> ptr, std::ostream& os) {
  os << Brief(ptr);
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void ShortPrint(
    TaggedImpl<HeapObjectReferenceType::STRONG, Address> ptr, std::ostream& os);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void ShortPrint(
    TaggedImpl<HeapObjectReferenceType::WEAK, Address> ptr, std::ostream& os);

#ifdef OBJECT_PRINT
template <HeapObjectReferenceType kRefType, typename StorageType>
void Print(TaggedImpl<kRefType, StorageType> ptr) {
  StdoutStream os;
  Print(ptr, os);
  os << std::flush;
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Print(
    TaggedImpl<HeapObjectReferenceType::STRONG, Address> ptr);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Print(
    TaggedImpl<HeapObjectReferenceType::WEAK, Address> ptr);

template <HeapObjectReferenceType kRefType, typename StorageType>
void Print(TaggedImpl<kRefType, StorageType> ptr, std::ostream& os) {
  Tagged<Smi> smi;
  Tagged<HeapObject> heap_object;
  if (ptr.ToSmi(&smi)) {
    os << "Smi: " << std::hex << "0x" << smi.value();
    os << std::dec << " (" << smi.value() << ")\n";
  } else if (ptr.IsCleared()) {
    os << "[cleared]";
  } else if (ptr.GetHeapObjectIfWeak(&heap_object)) {
    os << "[weak] ";
    heap_object->HeapObjectPrint(os);
  } else if (ptr.GetHeapObjectIfStrong(&heap_object)) {
    heap_object->HeapObjectPrint(os);
  } else {
    UNREACHABLE();
  }
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Print(
    TaggedImpl<HeapObjectReferenceType::STRONG, Address> ptr, std::ostream& os);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Print(
    TaggedImpl<HeapObjectReferenceType::WEAK, Address> ptr, std::ostream& os);
#endif  // OBJECT_PRINT

// Explicit instantiation declarations.
template class TaggedImpl<HeapObjectReferenceType::STRONG, Address>;
template class TaggedImpl<HeapObjectReferenceType::WEAK, Address>;

}  // namespace internal
}  // namespace v8

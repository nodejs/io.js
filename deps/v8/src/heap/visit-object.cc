// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/visit-object.h"

#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"
#include "src/heap/heap-visitor-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instruction-stream.h"
#include "src/objects/visitors.h"

namespace v8::internal {

class ObjectVisitorForwarder final
    : public HeapVisitor<ObjectVisitorForwarder> {
 public:
  explicit ObjectVisitorForwarder(Isolate* isolate, ObjectVisitor* visitor)
      : HeapVisitor(isolate), visitor_(visitor) {}
  explicit ObjectVisitorForwarder(LocalIsolate* isolate, ObjectVisitor* visitor)
      : HeapVisitor(isolate), visitor_(visitor) {}

  static constexpr bool ShouldVisitMapPointer() { return false; }
  static constexpr bool ShouldUseUncheckedCast() { return true; }
  static constexpr bool ShouldVisitFullJSObject() { return true; }

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {
    visitor_->VisitPointers(host, start, end);
  }

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override {
    visitor_->VisitPointers(host, start, end);
  }

  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    visitor_->VisitInstructionStreamPointer(host, slot);
  }

  void VisitCustomWeakPointers(Tagged<HeapObject> host, ObjectSlot start,
                               ObjectSlot end) override {
    visitor_->VisitCustomWeakPointers(host, start, end);
  }

  void VisitPointer(Tagged<HeapObject> host, ObjectSlot slot) override {
    visitor_->VisitPointers(host, slot, slot + 1);
  }

  void VisitPointer(Tagged<HeapObject> host, MaybeObjectSlot slot) override {
    visitor_->VisitPointers(host, slot, slot + 1);
  }

  void VisitCustomWeakPointer(Tagged<HeapObject> host,
                              ObjectSlot slot) override {
    visitor_->VisitCustomWeakPointer(host, slot);
  }

  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* rinfo) override {
    visitor_->VisitCodeTarget(host, rinfo);
  }

  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* rinfo) override {
    visitor_->VisitEmbeddedPointer(host, rinfo);
  }

  void VisitExternalReference(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) override {
    visitor_->VisitExternalReference(host, rinfo);
  }

  void VisitInternalReference(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) override {
    visitor_->VisitInternalReference(host, rinfo);
  }

  void VisitOffHeapTarget(Tagged<InstructionStream> host,
                          RelocInfo* rinfo) override {
    visitor_->VisitOffHeapTarget(host, rinfo);
  }

  void VisitExternalPointer(Tagged<HeapObject> host,
                            ExternalPointerSlot slot) override {
    visitor_->VisitExternalPointer(host, slot);
  }

  void VisitCppHeapPointer(Tagged<HeapObject> host,
                           CppHeapPointerSlot slot) override {
    visitor_->VisitCppHeapPointer(host, slot);
  }

  void VisitEphemeron(Tagged<HeapObject> host, int index, ObjectSlot key,
                      ObjectSlot value) override {
    visitor_->VisitEphemeron(host, index, key, value);
  }

  void VisitIndirectPointer(Tagged<HeapObject> host, IndirectPointerSlot slot,
                            IndirectPointerMode mode) override {
    visitor_->VisitIndirectPointer(host, slot, mode);
  }

  void VisitProtectedPointer(Tagged<TrustedObject> host,
                             ProtectedPointerSlot slot) override {
    visitor_->VisitProtectedPointer(host, slot);
  }

  void VisitProtectedPointer(Tagged<TrustedObject> host,
                             ProtectedMaybeObjectSlot slot) override {
    visitor_->VisitProtectedPointer(host, slot);
  }

  void VisitTrustedPointerTableEntry(Tagged<HeapObject> host,
                                     IndirectPointerSlot slot) override {
    visitor_->VisitTrustedPointerTableEntry(host, slot);
  }

  void VisitJSDispatchTableEntry(Tagged<HeapObject> host,
                                 JSDispatchHandle handle) override {
    visitor_->VisitJSDispatchTableEntry(host, handle);
  }

  void VisitMapPointer(Tagged<HeapObject> host) override { UNREACHABLE(); }

 private:
  ObjectVisitor* const visitor_;
};

void VisitObject(Isolate* isolate, Tagged<HeapObject> object,
                 ObjectVisitor* visitor) {
  visitor->VisitMapPointer(object);
  ObjectVisitorForwarder forward_visitor(isolate, visitor);
  forward_visitor.Visit(object);
}

void VisitObject(LocalIsolate* isolate, Tagged<HeapObject> object,
                 ObjectVisitor* visitor) {
  visitor->VisitMapPointer(object);
  ObjectVisitorForwarder forward_visitor(isolate, visitor);
  forward_visitor.Visit(object);
}

void VisitObjectBody(Isolate* isolate, Tagged<HeapObject> object,
                     ObjectVisitor* visitor) {
  ObjectVisitorForwarder forward_visitor(isolate, visitor);
  forward_visitor.Visit(object);
}

void VisitObjectBody(Isolate* isolate, Tagged<Map> map,
                     Tagged<HeapObject> object, ObjectVisitor* visitor) {
  ObjectVisitorForwarder forward_visitor(isolate, visitor);
  forward_visitor.Visit(map, object);
}

void VisitObjectBody(LocalIsolate* isolate, Tagged<HeapObject> object,
                     ObjectVisitor* visitor) {
  ObjectVisitorForwarder forward_visitor(isolate, visitor);
  forward_visitor.Visit(object);
}

}  // namespace v8::internal

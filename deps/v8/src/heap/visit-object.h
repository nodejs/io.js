// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_VISIT_OBJECT_H_
#define V8_HEAP_VISIT_OBJECT_H_

#include "src/objects/heap-object.h"

namespace v8::internal {

class Isolate;
class ObjectVisitor;

void VisitObject(Isolate* isolate, Tagged<HeapObject> object,
                 ObjectVisitor* visitor);
void VisitObject(LocalIsolate* isolate, Tagged<HeapObject> object,
                 ObjectVisitor* visitor);
void VisitObjectBody(Isolate* isolate, Tagged<HeapObject> object,
                     ObjectVisitor* visitor);
void VisitObjectBody(Isolate* isolate, Tagged<Map> map,
                     Tagged<HeapObject> object, ObjectVisitor* visitor);
void VisitObjectBody(LocalIsolate* isolate, Tagged<HeapObject> object,
                     ObjectVisitor* visitor);

}  // namespace v8::internal

#endif  // V8_HEAP_VISIT_OBJECT_H_

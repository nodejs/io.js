// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_LAYOUT_TRACER_H_
#define V8_HEAP_HEAP_LAYOUT_TRACER_H_

#include "include/v8-callbacks.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class Heap;
class MemoryChunkMetadata;

class HeapLayoutTracer : AllStatic {
 public:
  static void GCProloguePrintHeapLayout(v8::Isolate* isolate,
                                        v8::GCType gc_type,
                                        v8::GCCallbackFlags flags, void* data);
  static void GCEpiloguePrintHeapLayout(v8::Isolate* isolate,
                                        v8::GCType gc_type,
                                        v8::GCCallbackFlags flags, void* data);

 private:
  static void PrintBasicMemoryChunk(std::ostream& os,
                                    const MemoryChunkMetadata& chunk,
                                    const char* owner_name);
  static void PrintHeapLayout(std::ostream& os, Heap* heap);
};
}  // namespace internal
}  // namespace v8
#endif  // V8_HEAP_HEAP_LAYOUT_TRACER_H_

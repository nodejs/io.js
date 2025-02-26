// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/code-stats.h"

#include "src/codegen/reloc-info.h"
#include "src/heap/heap-inl.h"
#include "src/heap/large-spaces.h"
#include "src/heap/paged-spaces-inl.h"  // For PagedSpaceObjectIterator.
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// Record code statistics.
void CodeStatistics::RecordCodeAndMetadataStatistics(Tagged<HeapObject> object,
                                                     Isolate* isolate) {
  PtrComprCageBase cage_base(isolate);
  if (IsScript(object, cage_base)) {
    Tagged<Script> script = Cast<Script>(object);
    // Log the size of external source code.
    Tagged<Object> source = script->source(cage_base);
    if (IsExternalString(source, cage_base)) {
      Tagged<ExternalString> external_source_string =
          Cast<ExternalString>(source);
      int size = isolate->external_script_source_size();
      size += external_source_string->ExternalPayloadSize();
      isolate->set_external_script_source_size(size);
    }
  } else if (IsAbstractCode(object, cage_base)) {
    // Record code+metadata statistics.
    Tagged<AbstractCode> abstract_code = Cast<AbstractCode>(object);
    int size = abstract_code->SizeIncludingMetadata(cage_base);
    if (IsCode(abstract_code, cage_base)) {
      size += isolate->code_and_metadata_size();
      isolate->set_code_and_metadata_size(size);
    } else {
      size += isolate->bytecode_and_metadata_size();
      isolate->set_bytecode_and_metadata_size(size);
    }

#ifdef DEBUG
    CodeKind code_kind = abstract_code->kind(cage_base);
    isolate->code_kind_statistics()[static_cast<int>(code_kind)] +=
        abstract_code->Size(cage_base);
#endif
  }
}

void CodeStatistics::ResetCodeAndMetadataStatistics(Isolate* isolate) {
  isolate->set_code_and_metadata_size(0);
  isolate->set_bytecode_and_metadata_size(0);
  isolate->set_external_script_source_size(0);
#ifdef DEBUG
  ResetCodeStatistics(isolate);
#endif
}

// Collects code size statistics:
// - code and metadata size
// - by code kind (only in debug mode)
void CodeStatistics::CollectCodeStatistics(PagedSpace* space,
                                           Isolate* isolate) {
  PagedSpaceObjectIterator obj_it(isolate->heap(), space);
  for (Tagged<HeapObject> obj = obj_it.Next(); !obj.is_null();
       obj = obj_it.Next()) {
    RecordCodeAndMetadataStatistics(obj, isolate);
  }
}

// Collects code size statistics in OldLargeObjectSpace:
// - code and metadata size
// - by code kind (only in debug mode)
void CodeStatistics::CollectCodeStatistics(OldLargeObjectSpace* space,
                                           Isolate* isolate) {
  LargeObjectSpaceObjectIterator obj_it(space);
  for (Tagged<HeapObject> obj = obj_it.Next(); !obj.is_null();
       obj = obj_it.Next()) {
    RecordCodeAndMetadataStatistics(obj, isolate);
  }
}

#ifdef DEBUG
void CodeStatistics::ReportCodeStatistics(Isolate* isolate) {
  // Report code kind statistics
  int* code_kind_statistics = isolate->code_kind_statistics();
  PrintF("\n   Code kind histograms: \n");
  for (int i = 0; i < kCodeKindCount; i++) {
    if (code_kind_statistics[i] > 0) {
      PrintF("     %-20s: %10d bytes\n",
             CodeKindToString(static_cast<CodeKind>(i)),
             code_kind_statistics[i]);
    }
  }
  PrintF("\n");

  // Report code and metadata statistics
  if (isolate->code_and_metadata_size() > 0) {
    PrintF("Code size including metadata    : %10d bytes\n",
           isolate->code_and_metadata_size());
  }
  if (isolate->bytecode_and_metadata_size() > 0) {
    PrintF("Bytecode size including metadata: %10d bytes\n",
           isolate->bytecode_and_metadata_size());
  }

  PrintF("\n");
}

void CodeStatistics::ResetCodeStatistics(Isolate* isolate) {
  // Clear code kind statistics
  int* code_kind_statistics = isolate->code_kind_statistics();
  for (int i = 0; i < kCodeKindCount; i++) {
    code_kind_statistics[i] = 0;
  }
}
#endif

}  // namespace internal
}  // namespace v8

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/code-stats.h"

#include "src/codegen/code-comments.h"
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
    // Record code kind and code comment statistics.
    CodeKind code_kind = abstract_code->kind(cage_base);
    isolate->code_kind_statistics()[static_cast<int>(code_kind)] +=
        abstract_code->Size(cage_base);
    CodeStatistics::CollectCodeCommentStatistics(abstract_code, isolate);
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
// - by code comment (only in debug mode)
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
// - by code comment (only in debug mode)
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

  // Report code comment statistics
  CommentStatistic* comments_statistics =
      isolate->paged_space_comments_statistics();
  PrintF(
      "Code comment statistics (\"   [ comment-txt   :    size/   "
      "count  (average)\"):\n");
  for (int i = 0; i <= CommentStatistic::kMaxComments; i++) {
    const CommentStatistic& cs = comments_statistics[i];
    if (cs.size > 0) {
      PrintF("   %-30s: %10d/%6d     (%d)\n", cs.comment, cs.size, cs.count,
             cs.size / cs.count);
    }
  }
  PrintF("\n");
}

void CodeStatistics::ResetCodeStatistics(Isolate* isolate) {
  // Clear code kind statistics
  int* code_kind_statistics = isolate->code_kind_statistics();
  for (int i = 0; i < kCodeKindCount; i++) {
    code_kind_statistics[i] = 0;
  }

  // Clear code comment statistics
  CommentStatistic* comments_statistics =
      isolate->paged_space_comments_statistics();
  for (int i = 0; i < CommentStatistic::kMaxComments; i++) {
    comments_statistics[i].Clear();
  }
  comments_statistics[CommentStatistic::kMaxComments].comment = "Unknown";
  comments_statistics[CommentStatistic::kMaxComments].size = 0;
  comments_statistics[CommentStatistic::kMaxComments].count = 0;
}

// Adds comment to 'comment_statistics' table. Performance OK as long as
// 'kMaxComments' is small
void CodeStatistics::EnterComment(Isolate* isolate, const char* comment,
                                  int delta) {
  CommentStatistic* comments_statistics =
      isolate->paged_space_comments_statistics();
  // Do not count empty comments
  if (delta <= 0) return;
  CommentStatistic* cs = &comments_statistics[CommentStatistic::kMaxComments];
  // Search for a free or matching entry in 'comments_statistics': 'cs'
  // points to result.
  for (int i = 0; i < CommentStatistic::kMaxComments; i++) {
    if (comments_statistics[i].comment == nullptr) {
      cs = &comments_statistics[i];
      cs->comment = comment;
      break;
    } else if (strcmp(comments_statistics[i].comment, comment) == 0) {
      cs = &comments_statistics[i];
      break;
    }
  }
  // Update entry for 'comment'
  cs->size += delta;
  cs->count += 1;
}

// Call for each nested comment start (start marked with '[ xxx', end marked
// with ']'.  RelocIterator 'it' must point to a comment reloc info.
void CodeStatistics::CollectCommentStatistics(Isolate* isolate,
                                              CodeCommentsIterator* cit) {
  DCHECK(cit->HasCurrent());
  const char* comment_txt = cit->GetComment();
  if (comment_txt[0] != '[') {
    // Not a nested comment; skip
    return;
  }

  // Search for end of nested comment or a new nested comment
  int prev_pc_offset = cit->GetPCOffset();
  int flat_delta = 0;
  cit->Next();
  for (; cit->HasCurrent(); cit->Next()) {
    // All nested comments must be terminated properly, and therefore exit
    // from loop.
    const char* const txt = cit->GetComment();
    flat_delta += cit->GetPCOffset() - prev_pc_offset;
    if (txt[0] == ']') break;  // End of nested  comment
    // A new comment
    CollectCommentStatistics(isolate, cit);
    // Skip code that was covered with previous comment
    prev_pc_offset = cit->GetPCOffset();
  }
  EnterComment(isolate, comment_txt, flat_delta);
}

// Collects code comment statistics.
void CodeStatistics::CollectCodeCommentStatistics(Tagged<AbstractCode> obj,
                                                  Isolate* isolate) {
  // Bytecode objects do not contain RelocInfo.
  PtrComprCageBase cage_base{isolate};
  if (!IsCode(obj, cage_base)) return;

  Tagged<Code> code = Cast<Code>(obj);

  // Off-heap builtins might contain comments but they are a part of binary so
  // it doesn't make sense to account them in the stats.
  if (!code->has_instruction_stream()) return;

  CodeCommentsIterator cit(code->code_comments(), code->code_comments_size());
  int delta = 0;
  int prev_pc_offset = 0;
  while (cit.HasCurrent()) {
    delta += static_cast<int>(cit.GetPCOffset() - prev_pc_offset);
    CollectCommentStatistics(isolate, &cit);
    prev_pc_offset = cit.GetPCOffset();
    cit.Next();
  }

  DCHECK(0 <= prev_pc_offset && prev_pc_offset <= code->instruction_size());
  delta += static_cast<int>(code->instruction_size() - prev_pc_offset);
  EnterComment(isolate, "NoComment", delta);
}
#endif

}  // namespace internal
}  // namespace v8

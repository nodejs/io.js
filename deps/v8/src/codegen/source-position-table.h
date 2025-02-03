// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SOURCE_POSITION_TABLE_H_
#define V8_CODEGEN_SOURCE_POSITION_TABLE_H_

#include "src/base/export-template.h"
#include "src/base/vector.h"
#include "src/codegen/source-position.h"
#include "src/common/assert-scope.h"
#include "src/common/checks.h"
#include "src/common/globals.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class TrustedByteArray;
class Zone;

struct PositionTableEntry {
  PositionTableEntry()
      : source_position(0),
        code_offset(kFunctionEntryBytecodeOffset),
        is_statement(false) {}
  PositionTableEntry(int offset, int64_t source, bool statement)
      : source_position(source), code_offset(offset), is_statement(statement) {}

  int64_t source_position;
  int code_offset;
  bool is_statement;
};

class V8_EXPORT_PRIVATE SourcePositionTableBuilder {
 public:
  enum RecordingMode {
    // Indicates that source positions are never to be generated. (Resulting in
    // an empty table).
    OMIT_SOURCE_POSITIONS,
    // Indicates that source positions are not currently required, but may be
    // generated later.
    LAZY_SOURCE_POSITIONS,
    // Indicates that source positions should be immediately generated.
    RECORD_SOURCE_POSITIONS
  };

  explicit SourcePositionTableBuilder(
      Zone* zone, RecordingMode mode = RECORD_SOURCE_POSITIONS);

  void AddPosition(size_t code_offset, SourcePosition source_position,
                   bool is_statement);

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  Handle<TrustedByteArray> ToSourcePositionTable(IsolateT* isolate);
  base::OwnedVector<uint8_t> ToSourcePositionTableVector();

  inline bool Omit() const { return mode_ != RECORD_SOURCE_POSITIONS; }
  inline bool Lazy() const { return mode_ == LAZY_SOURCE_POSITIONS; }

 private:
  void AddEntry(const PositionTableEntry& entry);

  RecordingMode mode_;
  ZoneVector<uint8_t> bytes_;
#ifdef ENABLE_SLOW_DCHECKS
  ZoneVector<PositionTableEntry> raw_entries_;
#endif
  PositionTableEntry previous_;  // Previously written entry, to compute delta.
};

class V8_EXPORT_PRIVATE SourcePositionTableIterator {
 public:
  // Filter that applies when advancing the iterator. If the filter isn't
  // satisfied, we advance the iterator again.
  enum IterationFilter { kJavaScriptOnly = 0, kExternalOnly = 1, kAll = 2 };
  // Filter that applies only to the first entry of the source position table.
  // If it is kSkipFunctionEntry, it will skip the FunctionEntry entry if it
  // exists.
  enum FunctionEntryFilter {
    kSkipFunctionEntry = 0,
    kDontSkipFunctionEntry = 1
  };

  // Used for saving/restoring the iterator.
  struct IndexAndPositionState {
    int index_;
    PositionTableEntry position_;
    IterationFilter iteration_filter_;
    FunctionEntryFilter function_entry_filter_;
  };

  // We expose three flavours of the iterator, depending on the argument passed
  // to the constructor:

  // Handlified iterator allows allocation, but it needs a handle (and thus
  // a handle scope). This is the preferred version.
  explicit SourcePositionTableIterator(
      Handle<TrustedByteArray> byte_array,
      IterationFilter iteration_filter = kJavaScriptOnly,
      FunctionEntryFilter function_entry_filter = kSkipFunctionEntry);

  // Non-handlified iterator does not need a handle scope, but it disallows
  // allocation during its lifetime. This is useful if there is no handle
  // scope around.
  explicit SourcePositionTableIterator(
      Tagged<TrustedByteArray> byte_array,
      IterationFilter iteration_filter = kJavaScriptOnly,
      FunctionEntryFilter function_entry_filter = kSkipFunctionEntry);

  // Handle-safe iterator based on an a vector located outside the garbage
  // collected heap, allows allocation during its lifetime.
  explicit SourcePositionTableIterator(
      base::Vector<const uint8_t> bytes,
      IterationFilter iteration_filter = kJavaScriptOnly,
      FunctionEntryFilter function_entry_filter = kSkipFunctionEntry);

  void Advance();

  int code_offset() const {
    DCHECK(!done());
    return current_.code_offset;
  }
  SourcePosition source_position() const {
    DCHECK(!done());
    return SourcePosition::FromRaw(current_.source_position);
  }
  bool is_statement() const {
    DCHECK(!done());
    return current_.is_statement;
  }
  bool done() const { return index_ == kDone; }

  IndexAndPositionState GetState() const {
    return {index_, current_, iteration_filter_, function_entry_filter_};
  }

  void RestoreState(const IndexAndPositionState& saved_state) {
    index_ = saved_state.index_;
    current_ = saved_state.position_;
    iteration_filter_ = saved_state.iteration_filter_;
    function_entry_filter_ = saved_state.function_entry_filter_;
  }

 private:
  // Initializes the source position interator with the first valid bytecode.
  // Also sets the FunctionEntry SourcePosition if it exists.
  void Initialize();

  static const int kDone = -1;

  base::Vector<const uint8_t> raw_table_;
  Handle<TrustedByteArray> table_;
  int index_ = 0;
  PositionTableEntry current_;
  IterationFilter iteration_filter_;
  FunctionEntryFilter function_entry_filter_;
  DISALLOW_GARBAGE_COLLECTION(no_gc)
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_SOURCE_POSITION_TABLE_H_

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/source-position-table.h"

#include "src/base/export-template.h"
#include "src/base/logging.h"
#include "src/common/assert-scope.h"
#include "src/heap/local-factory-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

// We'll use a simple encoding scheme to record the source positions.
// Conceptually, each position consists of:
// - code_offset: An integer index into the BytecodeArray or code.
// - source_position: An integer index into the source string.
// - position type: Each position is either a statement or an expression.
//
// The basic idea for the encoding is to use a variable-length integer coding,
// where each byte contains 7 bits of payload data, and 1 'more' bit that
// determines whether additional bytes follow. Additionally:
// - we record the difference from the previous position,
// - we just stuff one bit for the type into the code offset,
// - we write least-significant bits first,
// - we use zig-zag encoding to encode both positive and negative numbers.

namespace {

// Each byte is encoded as MoreBit | ValueBits.
using MoreBit = base::BitField8<bool, 7, 1>;
using ValueBits = base::BitField8<unsigned, 0, 7>;

// Helper: Add the offsets from 'other' to 'value'. Also set is_statement.
void AddAndSetEntry(PositionTableEntry* value,
                    const PositionTableEntry& other) {
  value->code_offset += other.code_offset;
  DCHECK_IMPLIES(value->code_offset != kFunctionEntryBytecodeOffset,
                 value->code_offset >= 0);
  value->source_position += other.source_position;
  DCHECK_LE(0, value->source_position);
  value->is_statement = other.is_statement;
}

// Helper: Subtract the offsets from 'other' from 'value'.
void SubtractFromEntry(PositionTableEntry* value,
                       const PositionTableEntry& other) {
  value->code_offset -= other.code_offset;
  value->source_position -= other.source_position;
}

// Helper: Encode an integer.
template <typename T>
void EncodeInt(ZoneVector<uint8_t>* bytes, T value) {
  using unsigned_type = typename std::make_unsigned<T>::type;
  // Zig-zag encoding.
  static constexpr int kShift = sizeof(T) * kBitsPerByte - 1;
  value = ((static_cast<unsigned_type>(value) << 1) ^ (value >> kShift));
  DCHECK_GE(value, 0);
  unsigned_type encoded = static_cast<unsigned_type>(value);
  bool more;
  do {
    more = encoded > ValueBits::kMax;
    uint8_t current =
        MoreBit::encode(more) | ValueBits::encode(encoded & ValueBits::kMask);
    bytes->push_back(current);
    encoded >>= ValueBits::kSize;
  } while (more);
}

// Encode a PositionTableEntry.
void EncodeEntry(ZoneVector<uint8_t>* bytes, const PositionTableEntry& entry) {
  // We only accept ascending code offsets.
  DCHECK_LE(0, entry.code_offset);
  // All but the first entry must be *strictly* ascending (no two entries for
  // the same position).
  // TODO(11496): This DCHECK fails tests.
  // DCHECK_IMPLIES(!bytes->empty(), entry.code_offset > 0);
  // Since code_offset is not negative, we use sign to encode is_statement.
  EncodeInt(bytes,
            entry.is_statement ? entry.code_offset : -entry.code_offset - 1);
  EncodeInt(bytes, entry.source_position);
}

// Helper: Decode an integer.
template <typename T>
T DecodeInt(base::Vector<const uint8_t> bytes, int* index) {
  uint8_t current;
  int shift = 0;
  T decoded = 0;
  bool more;
  do {
    current = bytes[(*index)++];
    decoded |= static_cast<typename std::make_unsigned<T>::type>(
                   ValueBits::decode(current))
               << shift;
    more = MoreBit::decode(current);
    shift += ValueBits::kSize;
  } while (more);
  DCHECK_GE(decoded, 0);
  decoded = (decoded >> 1) ^ (-(decoded & 1));
  return decoded;
}

void DecodeEntry(base::Vector<const uint8_t> bytes, int* index,
                 PositionTableEntry* entry) {
  int tmp = DecodeInt<int>(bytes, index);
  if (tmp >= 0) {
    entry->is_statement = true;
    entry->code_offset = tmp;
  } else {
    entry->is_statement = false;
    entry->code_offset = -(tmp + 1);
  }
  entry->source_position = DecodeInt<int64_t>(bytes, index);
}

base::Vector<const uint8_t> VectorFromByteArray(
    Tagged<TrustedByteArray> byte_array) {
  return base::Vector<const uint8_t>(byte_array->begin(), byte_array->length());
}

#ifdef ENABLE_SLOW_DCHECKS
void CheckTableEquals(const ZoneVector<PositionTableEntry>& raw_entries,
                      SourcePositionTableIterator* encoded) {
  // Brute force testing: Record all positions and decode
  // the entire table to verify they are identical.
  auto raw = raw_entries.begin();
  for (; !encoded->done(); encoded->Advance(), raw++) {
    DCHECK(raw != raw_entries.end());
    DCHECK_EQ(encoded->code_offset(), raw->code_offset);
    DCHECK_EQ(encoded->source_position().raw(), raw->source_position);
    DCHECK_EQ(encoded->is_statement(), raw->is_statement);
  }
  DCHECK(raw == raw_entries.end());
}
#endif

}  // namespace

SourcePositionTableBuilder::SourcePositionTableBuilder(
    Zone* zone, SourcePositionTableBuilder::RecordingMode mode)
    : mode_(mode),
      bytes_(zone),
#ifdef ENABLE_SLOW_DCHECKS
      raw_entries_(zone),
#endif
      previous_() {
}

void SourcePositionTableBuilder::AddPosition(size_t code_offset,
                                             SourcePosition source_position,
                                             bool is_statement) {
  if (Omit()) return;
  DCHECK(source_position.IsKnown());
  int offset = static_cast<int>(code_offset);
  AddEntry({offset, source_position.raw(), is_statement});
}

V8_INLINE void SourcePositionTableBuilder::AddEntry(
    const PositionTableEntry& entry) {
  PositionTableEntry tmp(entry);
  SubtractFromEntry(&tmp, previous_);
  EncodeEntry(&bytes_, tmp);
  previous_ = entry;
#ifdef ENABLE_SLOW_DCHECKS
  raw_entries_.push_back(entry);
#endif
}

template <typename IsolateT>
Handle<TrustedByteArray> SourcePositionTableBuilder::ToSourcePositionTable(
    IsolateT* isolate) {
  if (bytes_.empty()) return isolate->factory()->empty_trusted_byte_array();
  DCHECK(!Omit());

  Handle<TrustedByteArray> table =
      isolate->factory()->NewTrustedByteArray(static_cast<int>(bytes_.size()));
  MemCopy(table->begin(), bytes_.data(), bytes_.size());

#ifdef ENABLE_SLOW_DCHECKS
  // Brute force testing: Record all positions and decode
  // the entire table to verify they are identical.
  SourcePositionTableIterator it(
      *table, SourcePositionTableIterator::kAll,
      SourcePositionTableIterator::kDontSkipFunctionEntry);
  CheckTableEquals(raw_entries_, &it);
  // No additional source positions after creating the table.
  mode_ = OMIT_SOURCE_POSITIONS;
#endif
  return table;
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<TrustedByteArray> SourcePositionTableBuilder::ToSourcePositionTable(
        Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<TrustedByteArray> SourcePositionTableBuilder::ToSourcePositionTable(
        LocalIsolate* isolate);

base::OwnedVector<uint8_t>
SourcePositionTableBuilder::ToSourcePositionTableVector() {
  if (bytes_.empty()) return base::OwnedVector<uint8_t>();
  DCHECK(!Omit());

  base::OwnedVector<uint8_t> table = base::OwnedVector<uint8_t>::Of(bytes_);

#ifdef ENABLE_SLOW_DCHECKS
  // Brute force testing: Record all positions and decode
  // the entire table to verify they are identical.
  SourcePositionTableIterator it(
      table.as_vector(), SourcePositionTableIterator::kAll,
      SourcePositionTableIterator::kDontSkipFunctionEntry);
  CheckTableEquals(raw_entries_, &it);
  // No additional source positions after creating the table.
  mode_ = OMIT_SOURCE_POSITIONS;
#endif
  return table;
}

void SourcePositionTableIterator::Initialize() {
  Advance();
  if (function_entry_filter_ == kSkipFunctionEntry &&
      current_.code_offset == kFunctionEntryBytecodeOffset && !done()) {
    Advance();
  }
}

SourcePositionTableIterator::SourcePositionTableIterator(
    Tagged<TrustedByteArray> byte_array, IterationFilter iteration_filter,
    FunctionEntryFilter function_entry_filter)
    : raw_table_(VectorFromByteArray(byte_array)),
      iteration_filter_(iteration_filter),
      function_entry_filter_(function_entry_filter) {
  Initialize();
}

SourcePositionTableIterator::SourcePositionTableIterator(
    Handle<TrustedByteArray> byte_array, IterationFilter iteration_filter,
    FunctionEntryFilter function_entry_filter)
    : table_(byte_array),
      iteration_filter_(iteration_filter),
      function_entry_filter_(function_entry_filter) {
  Initialize();
#ifdef DEBUG
  // We can enable allocation because we keep the table in a handle.
  no_gc.Release();
#endif  // DEBUG
}

SourcePositionTableIterator::SourcePositionTableIterator(
    base::Vector<const uint8_t> bytes, IterationFilter iteration_filter,
    FunctionEntryFilter function_entry_filter)
    : raw_table_(bytes),
      iteration_filter_(iteration_filter),
      function_entry_filter_(function_entry_filter) {
  Initialize();
#ifdef DEBUG
  // We can enable allocation because the underlying vector does not move.
  no_gc.Release();
#endif  // DEBUG
}

void SourcePositionTableIterator::Advance() {
  base::Vector<const uint8_t> bytes =
      table_.is_null() ? raw_table_ : VectorFromByteArray(*table_);
  DCHECK(!done());
  DCHECK(index_ >= 0 && index_ <= bytes.length());
  bool filter_satisfied = false;
  while (!done() && !filter_satisfied) {
    if (index_ >= bytes.length()) {
      index_ = kDone;
    } else {
      PositionTableEntry tmp;
      DecodeEntry(bytes, &index_, &tmp);
      AddAndSetEntry(&current_, tmp);
      SourcePosition p = source_position();
      filter_satisfied =
          (iteration_filter_ == kAll) ||
          (iteration_filter_ == kJavaScriptOnly && p.IsJavaScript()) ||
          (iteration_filter_ == kExternalOnly && p.IsExternal());
    }
  }
}

}  // namespace internal
}  // namespace v8

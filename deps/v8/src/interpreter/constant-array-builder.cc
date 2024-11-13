// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/constant-array-builder.h"

#include <cmath>
#include <functional>
#include <set>

#include "src/ast/ast-value-factory.h"
#include "src/ast/scopes.h"
#include "src/base/functional.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/local-factory-inl.h"
#include "src/interpreter/bytecode-operands.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

ConstantArrayBuilder::ConstantArraySlice::ConstantArraySlice(
    Zone* zone, size_t start_index, size_t capacity, OperandSize operand_size)
    : start_index_(start_index),
      capacity_(capacity),
      reserved_(0),
      operand_size_(operand_size),
      constants_(zone) {}

void ConstantArrayBuilder::ConstantArraySlice::Reserve() {
  DCHECK_GT(available(), 0u);
  reserved_++;
  DCHECK_LE(reserved_, capacity() - constants_.size());
}

void ConstantArrayBuilder::ConstantArraySlice::Unreserve() {
  DCHECK_GT(reserved_, 0u);
  reserved_--;
}

size_t ConstantArrayBuilder::ConstantArraySlice::Allocate(
    ConstantArrayBuilder::Entry entry, size_t count) {
  DCHECK_GE(available(), count);
  size_t index = constants_.size();
  DCHECK_LT(index, capacity());
  for (size_t i = 0; i < count; ++i) {
    constants_.push_back(entry);
  }
  return index + start_index();
}

ConstantArrayBuilder::Entry& ConstantArrayBuilder::ConstantArraySlice::At(
    size_t index) {
  DCHECK_GE(index, start_index());
  DCHECK_LT(index, start_index() + size());
  return constants_[index - start_index()];
}

const ConstantArrayBuilder::Entry& ConstantArrayBuilder::ConstantArraySlice::At(
    size_t index) const {
  DCHECK_GE(index, start_index());
  DCHECK_LT(index, start_index() + size());
  return constants_[index - start_index()];
}

#if DEBUG
template <typename IsolateT>
void ConstantArrayBuilder::ConstantArraySlice::CheckAllElementsAreUnique(
    IsolateT* isolate) const {
  std::set<Tagged<Smi>> smis;
  std::set<double> heap_numbers;
  std::set<const AstRawString*> strings;
  std::set<const char*> bigints;
  std::set<const Scope*> scopes;
  std::set<Tagged<Object>, Object::Comparer> deferred_objects;
  for (const Entry& entry : constants_) {
    bool duplicate = false;
    switch (entry.tag_) {
      case Entry::Tag::kSmi:
        duplicate = !smis.insert(entry.smi_).second;
        break;
      case Entry::Tag::kHeapNumber:
        duplicate = !heap_numbers.insert(entry.heap_number_).second;
        break;
      case Entry::Tag::kRawString:
        duplicate = !strings.insert(entry.raw_string_).second;
        break;
      case Entry::Tag::kBigInt:
        duplicate = !bigints.insert(entry.bigint_.c_str()).second;
        break;
      case Entry::Tag::kScope:
        duplicate = !scopes.insert(entry.scope_).second;
        break;
      case Entry::Tag::kHandle:
        duplicate = !deferred_objects.insert(*entry.handle_).second;
        break;
      case Entry::Tag::kDeferred:
        UNREACHABLE();  // Should be kHandle at this point.
      case Entry::Tag::kJumpTableSmi:
      case Entry::Tag::kUninitializedJumpTableSmi:
        // TODO(leszeks): Ignore jump tables because they have to be contiguous,
        // so they can contain duplicates.
        break;
#define CASE_TAG(NAME, ...) case Entry::Tag::k##NAME:
        SINGLETON_CONSTANT_ENTRY_TYPES(CASE_TAG)
#undef CASE_TAG
        // Singletons are non-duplicated by definition.
        break;
    }
    if (duplicate) {
      std::ostringstream os;
      os << "Duplicate constant found: " << Brief(*entry.ToHandle(isolate))
         << std::endl;
      // Print all the entries in the slice to help debug duplicates.
      size_t i = start_index();
      for (const Entry& prev_entry : constants_) {
        os << i++ << ": " << Brief(*prev_entry.ToHandle(isolate)) << std::endl;
      }
      FATAL("%s", os.str().c_str());
    }
  }
}
#endif

STATIC_CONST_MEMBER_DEFINITION const size_t ConstantArrayBuilder::k8BitCapacity;
STATIC_CONST_MEMBER_DEFINITION const size_t
    ConstantArrayBuilder::k16BitCapacity;
STATIC_CONST_MEMBER_DEFINITION const size_t
    ConstantArrayBuilder::k32BitCapacity;

ConstantArrayBuilder::ConstantArrayBuilder(Zone* zone)
    : constants_map_(16, base::KeyEqualityMatcher<intptr_t>(),
                     ZoneAllocationPolicy(zone)),
      smi_map_(zone),
      smi_pairs_(zone),
      heap_number_map_(zone) {
  idx_slice_[0] =
      zone->New<ConstantArraySlice>(zone, 0, k8BitCapacity, OperandSize::kByte);
  idx_slice_[1] = zone->New<ConstantArraySlice>(
      zone, k8BitCapacity, k16BitCapacity, OperandSize::kShort);
  idx_slice_[2] = zone->New<ConstantArraySlice>(
      zone, k8BitCapacity + k16BitCapacity, k32BitCapacity, OperandSize::kQuad);
}

size_t ConstantArrayBuilder::size() const {
  size_t i = arraysize(idx_slice_);
  while (i > 0) {
    ConstantArraySlice* slice = idx_slice_[--i];
    if (slice->size() > 0) {
      return slice->start_index() + slice->size();
    }
  }
  return idx_slice_[0]->size();
}

ConstantArrayBuilder::ConstantArraySlice* ConstantArrayBuilder::IndexToSlice(
    size_t index) const {
  for (ConstantArraySlice* slice : idx_slice_) {
    if (index <= slice->max_index()) {
      return slice;
    }
  }
  UNREACHABLE();
}

template <typename IsolateT>
MaybeHandle<Object> ConstantArrayBuilder::At(size_t index,
                                             IsolateT* isolate) const {
  const ConstantArraySlice* slice = IndexToSlice(index);
  DCHECK_LT(index, slice->capacity());
  if (index < slice->start_index() + slice->size()) {
    const Entry& entry = slice->At(index);
    if (!entry.IsDeferred()) return entry.ToHandle(isolate);
  }
  return MaybeHandle<Object>();
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    MaybeHandle<Object> ConstantArrayBuilder::At(size_t index,
                                                 Isolate* isolate) const;
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    MaybeHandle<Object> ConstantArrayBuilder::At(size_t index,
                                                 LocalIsolate* isolate) const;

template <typename IsolateT>
Handle<TrustedFixedArray> ConstantArrayBuilder::ToFixedArray(
    IsolateT* isolate) {
  Handle<TrustedFixedArray> fixed_array =
      isolate->factory()->NewTrustedFixedArray(static_cast<int>(size()));
  MemsetTagged(fixed_array->RawFieldOfFirstElement(),
               *isolate->factory()->the_hole_value(), size());
  int array_index = 0;
  for (const ConstantArraySlice* slice : idx_slice_) {
    DCHECK_EQ(slice->reserved(), 0);
    DCHECK(array_index == 0 ||
           base::bits::IsPowerOfTwo(static_cast<uint32_t>(array_index)));
#if DEBUG
    // Different slices might contain the same element due to reservations, but
    // all elements within a slice should be unique.
    slice->CheckAllElementsAreUnique(isolate);
#endif
    // Copy objects from slice into array.
    for (size_t i = 0; i < slice->size(); ++i) {
      DirectHandle<Object> value =
          slice->At(slice->start_index() + i).ToHandle(isolate);
      fixed_array->set(array_index++, *value);
    }
    // Leave holes where reservations led to unused slots.
    size_t padding = slice->capacity() - slice->size();
    if (static_cast<size_t>(fixed_array->length() - array_index) <= padding) {
      break;
    }
    array_index += padding;
  }
  DCHECK_GE(array_index, fixed_array->length());
  return fixed_array;
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<TrustedFixedArray> ConstantArrayBuilder::ToFixedArray(
        Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<TrustedFixedArray> ConstantArrayBuilder::ToFixedArray(
        LocalIsolate* isolate);

size_t ConstantArrayBuilder::Insert(Tagged<Smi> smi) {
  auto entry = smi_map_.find(smi);
  if (entry == smi_map_.end()) {
    return AllocateReservedEntry(smi);
  }
  return entry->second;
}

size_t ConstantArrayBuilder::Insert(double number) {
  if (std::isnan(number)) return InsertNaN();
  auto entry = heap_number_map_.find(number);
  if (entry == heap_number_map_.end()) {
    index_t index = static_cast<index_t>(AllocateIndex(Entry(number)));
    heap_number_map_[number] = index;
    return index;
  }
  return entry->second;
}

size_t ConstantArrayBuilder::Insert(const AstRawString* raw_string) {
  return constants_map_
      .LookupOrInsert(reinterpret_cast<intptr_t>(raw_string),
                      raw_string->Hash(),
                      [&]() { return AllocateIndex(Entry(raw_string)); })
      ->value;
}

size_t ConstantArrayBuilder::Insert(AstBigInt bigint) {
  return constants_map_
      .LookupOrInsert(reinterpret_cast<intptr_t>(bigint.c_str()),
                      static_cast<uint32_t>(base::hash_value(bigint.c_str())),
                      [&]() { return AllocateIndex(Entry(bigint)); })
      ->value;
}

size_t ConstantArrayBuilder::Insert(const Scope* scope) {
  return constants_map_
      .LookupOrInsert(reinterpret_cast<intptr_t>(scope),
                      static_cast<uint32_t>(base::hash_value(scope)),
                      [&]() { return AllocateIndex(Entry(scope)); })
      ->value;
}

#define INSERT_ENTRY(NAME, LOWER_NAME)              \
  size_t ConstantArrayBuilder::Insert##NAME() {     \
    if (LOWER_NAME##_ < 0) {                        \
      LOWER_NAME##_ = AllocateIndex(Entry::NAME()); \
    }                                               \
    return LOWER_NAME##_;                           \
  }
SINGLETON_CONSTANT_ENTRY_TYPES(INSERT_ENTRY)
#undef INSERT_ENTRY

ConstantArrayBuilder::index_t ConstantArrayBuilder::AllocateIndex(
    ConstantArrayBuilder::Entry entry) {
  return AllocateIndexArray(entry, 1);
}

ConstantArrayBuilder::index_t ConstantArrayBuilder::AllocateIndexArray(
    ConstantArrayBuilder::Entry entry, size_t count) {
  for (size_t i = 0; i < arraysize(idx_slice_); ++i) {
    if (idx_slice_[i]->available() >= count) {
      return static_cast<index_t>(idx_slice_[i]->Allocate(entry, count));
    }
  }
  UNREACHABLE();
}

ConstantArrayBuilder::ConstantArraySlice*
ConstantArrayBuilder::OperandSizeToSlice(OperandSize operand_size) const {
  ConstantArraySlice* slice = nullptr;
  switch (operand_size) {
    case OperandSize::kNone:
      UNREACHABLE();
    case OperandSize::kByte:
      slice = idx_slice_[0];
      break;
    case OperandSize::kShort:
      slice = idx_slice_[1];
      break;
    case OperandSize::kQuad:
      slice = idx_slice_[2];
      break;
  }
  DCHECK(slice->operand_size() == operand_size);
  return slice;
}

size_t ConstantArrayBuilder::InsertDeferred() {
  return AllocateIndex(Entry::Deferred());
}

size_t ConstantArrayBuilder::InsertJumpTable(size_t size) {
  return AllocateIndexArray(Entry::UninitializedJumpTableSmi(), size);
}

void ConstantArrayBuilder::SetDeferredAt(size_t index, Handle<Object> object) {
  ConstantArraySlice* slice = IndexToSlice(index);
  return slice->At(index).SetDeferred(object);
}

void ConstantArrayBuilder::SetJumpTableSmi(size_t index, Tagged<Smi> smi) {
  ConstantArraySlice* slice = IndexToSlice(index);
  // Allow others to reuse these Smis, but insert using emplace to avoid
  // overwriting existing values in the Smi map (which may have a smaller
  // operand size).
  smi_map_.emplace(smi, static_cast<index_t>(index));
  return slice->At(index).SetJumpTableSmi(smi);
}

OperandSize ConstantArrayBuilder::CreateReservedEntry(
    OperandSize minimum_operand_size) {
  for (size_t i = 0; i < arraysize(idx_slice_); ++i) {
    if (idx_slice_[i]->available() > 0 &&
        idx_slice_[i]->operand_size() >= minimum_operand_size) {
      idx_slice_[i]->Reserve();
      return idx_slice_[i]->operand_size();
    }
  }
  UNREACHABLE();
}

ConstantArrayBuilder::index_t ConstantArrayBuilder::AllocateReservedEntry(
    Tagged<Smi> value) {
  index_t index = static_cast<index_t>(AllocateIndex(Entry(value)));
  smi_map_[value] = index;
  return index;
}

size_t ConstantArrayBuilder::CommitReservedEntry(OperandSize operand_size,
                                                 Tagged<Smi> value) {
  DiscardReservedEntry(operand_size);
  size_t index;
  auto entry = smi_map_.find(value);
  if (entry == smi_map_.end()) {
    index = AllocateReservedEntry(value);
  } else {
    ConstantArraySlice* slice = OperandSizeToSlice(operand_size);
    index = entry->second;
    if (index > slice->max_index()) {
      // The object is already in the constant array, but may have an
      // index too big for the reserved operand_size. So, duplicate
      // entry with the smaller operand size.
      index = AllocateReservedEntry(value);
    }
    DCHECK_LE(index, slice->max_index());
  }
  return index;
}

void ConstantArrayBuilder::DiscardReservedEntry(OperandSize operand_size) {
  OperandSizeToSlice(operand_size)->Unreserve();
}

template <typename IsolateT>
Handle<Object> ConstantArrayBuilder::Entry::ToHandle(IsolateT* isolate) const {
  switch (tag_) {
    case Tag::kDeferred:
      // We shouldn't have any deferred entries by now.
      UNREACHABLE();
    case Tag::kHandle:
      return handle_;
    case Tag::kSmi:
    case Tag::kJumpTableSmi:
      return handle(smi_, isolate);
    case Tag::kUninitializedJumpTableSmi:
      // TODO(leszeks): There's probably a better value we could use here.
      return isolate->factory()->the_hole_value();
    case Tag::kRawString:
      return raw_string_->string();
    case Tag::kHeapNumber:
      return isolate->factory()->template NewNumber<AllocationType::kOld>(
          heap_number_);
    case Tag::kBigInt:
      // This should never fail: the parser will never create a BigInt
      // literal that cannot be allocated.
      return BigIntLiteral(isolate, bigint_.c_str()).ToHandleChecked();
    case Tag::kScope:
      return scope_->scope_info();
#define ENTRY_LOOKUP(Name, name) \
  case Tag::k##Name:             \
    return isolate->factory()->name();
      SINGLETON_CONSTANT_ENTRY_TYPES(ENTRY_LOOKUP);
#undef ENTRY_LOOKUP
  }
  UNREACHABLE();
}

template Handle<Object> ConstantArrayBuilder::Entry::ToHandle(
    Isolate* isolate) const;
template Handle<Object> ConstantArrayBuilder::Entry::ToHandle(
    LocalIsolate* isolate) const;

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

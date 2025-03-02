// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_CONSTANT_ARRAY_BUILDER_H_
#define V8_INTERPRETER_CONSTANT_ARRAY_BUILDER_H_

#include "src/ast/ast-value-factory.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-operands.h"
#include "src/objects/smi.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class Isolate;
class AstRawString;
class AstValue;

namespace interpreter {

// Constant array entries that represent singletons.
#define SINGLETON_CONSTANT_ENTRY_TYPES(V)                                    \
  V(AsyncIteratorSymbol, async_iterator_symbol)                              \
  V(ClassFieldsSymbol, class_fields_symbol)                                  \
  V(EmptyObjectBoilerplateDescription, empty_object_boilerplate_description) \
  V(EmptyArrayBoilerplateDescription, empty_array_boilerplate_description)   \
  V(EmptyFixedArray, empty_fixed_array)                                      \
  V(IteratorSymbol, iterator_symbol)                                         \
  V(InterpreterTrampolineSymbol, interpreter_trampoline_symbol)              \
  V(NaN, nan_value)

// A helper class for constructing constant arrays for the
// interpreter. Each instance of this class is intended to be used to
// generate exactly one FixedArray of constants via the ToFixedArray
// method.
class V8_EXPORT_PRIVATE ConstantArrayBuilder final {
 public:
  // Capacity of the 8-bit operand slice.
  static const size_t k8BitCapacity = 1u << kBitsPerByte;

  // Capacity of the 16-bit operand slice.
  static const size_t k16BitCapacity = (1u << 2 * kBitsPerByte) - k8BitCapacity;

  // Capacity of the 32-bit operand slice.
  static const size_t k32BitCapacity =
      kMaxUInt32 - k16BitCapacity - k8BitCapacity + 1;

  explicit ConstantArrayBuilder(Zone* zone);

  // Generate a fixed array of constant handles based on inserted objects.
  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  Handle<TrustedFixedArray> ToFixedArray(IsolateT* isolate);

  // Returns the object, as a handle in |isolate|, that is in the constant pool
  // array at index |index|. Returns null if there is no handle at this index.
  // Only expected to be used in tests.
  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  MaybeHandle<Object> At(size_t index, IsolateT* isolate) const;

  // Returns the number of elements in the array.
  size_t size() const;

  // Insert an object into the constants array if it is not already present.
  // Returns the array index associated with the object.
  size_t Insert(Tagged<Smi> smi);
  size_t Insert(double number);
  size_t Insert(const AstRawString* raw_string);
  size_t Insert(const AstConsString* cons_string);
  size_t Insert(AstBigInt bigint);
  size_t Insert(const Scope* scope);
#define INSERT_ENTRY(NAME, ...) size_t Insert##NAME();
  SINGLETON_CONSTANT_ENTRY_TYPES(INSERT_ENTRY)
#undef INSERT_ENTRY

  // Inserts an empty entry and returns the array index associated with the
  // reservation. The entry's handle value can be inserted by calling
  // SetDeferredAt().
  size_t InsertDeferred();

  // Inserts |size| consecutive empty entries and returns the array index
  // associated with the first reservation. Each entry's Smi value can be
  // inserted by calling SetJumpTableSmi().
  size_t InsertJumpTable(size_t size);

  // Sets the deferred value at |index| to |object|.
  void SetDeferredAt(size_t index, Handle<Object> object);

  // Sets the jump table entry at |index| to |smi|. Note that |index| is the
  // constant pool index, not the switch case value.
  void SetJumpTableSmi(size_t index, Tagged<Smi> smi);

  // Creates a reserved entry in the constant pool and returns
  // the size of the operand that'll be required to hold the entry
  // when committed.
  OperandSize CreateReservedEntry(
      OperandSize minimum_operand_size = OperandSize::kNone);

  // Commit reserved entry and returns the constant pool index for the
  // SMI value.
  size_t CommitReservedEntry(OperandSize operand_size, Tagged<Smi> value);

  // Discards constant pool reservation.
  void DiscardReservedEntry(OperandSize operand_size);

 private:
  using index_t = uint32_t;

  struct ConstantArraySlice;

  class Entry {
   private:
    enum class Tag : uint8_t;

   public:
    explicit Entry(Tagged<Smi> smi) : smi_(smi), tag_(Tag::kSmi) {}
    explicit Entry(double heap_number)
        : heap_number_(heap_number), tag_(Tag::kHeapNumber) {}
    explicit Entry(const AstRawString* raw_string)
        : raw_string_(raw_string), tag_(Tag::kRawString) {}
    explicit Entry(const AstConsString* cons_string)
        : cons_string_(cons_string), tag_(Tag::kConsString) {}
    explicit Entry(AstBigInt bigint) : bigint_(bigint), tag_(Tag::kBigInt) {}
    explicit Entry(const Scope* scope) : scope_(scope), tag_(Tag::kScope) {}

#define CONSTRUCT_ENTRY(NAME, LOWER_NAME) \
  static Entry NAME() { return Entry(Tag::k##NAME); }
    SINGLETON_CONSTANT_ENTRY_TYPES(CONSTRUCT_ENTRY)
#undef CONSTRUCT_ENTRY

    static Entry Deferred() { return Entry(Tag::kDeferred); }

    static Entry UninitializedJumpTableSmi() {
      return Entry(Tag::kUninitializedJumpTableSmi);
    }

    bool IsDeferred() const { return tag_ == Tag::kDeferred; }

    bool IsJumpTableEntry() const {
      return tag_ == Tag::kUninitializedJumpTableSmi ||
             tag_ == Tag::kJumpTableSmi;
    }

    void SetDeferred(Handle<Object> handle) {
      DCHECK_EQ(tag_, Tag::kDeferred);
      tag_ = Tag::kHandle;
      handle_ = handle;
    }

    void SetJumpTableSmi(Tagged<Smi> smi) {
      DCHECK_EQ(tag_, Tag::kUninitializedJumpTableSmi);
      tag_ = Tag::kJumpTableSmi;
      smi_ = smi;
    }

    template <typename IsolateT>
    Handle<Object> ToHandle(IsolateT* isolate) const;

   private:
    explicit Entry(Tag tag) : tag_(tag) {}

    union {
      IndirectHandle<Object> handle_;
      Tagged<Smi> smi_;
      double heap_number_;
      const AstRawString* raw_string_;
      const AstConsString* cons_string_;
      AstBigInt bigint_;
      const Scope* scope_;
    };

    enum class Tag : uint8_t {
      kDeferred,
      kHandle,
      kSmi,
      kRawString,
      kConsString,
      kHeapNumber,
      kBigInt,
      kScope,
      kUninitializedJumpTableSmi,
      kJumpTableSmi,
#define ENTRY_TAG(NAME, ...) k##NAME,
      SINGLETON_CONSTANT_ENTRY_TYPES(ENTRY_TAG)
#undef ENTRY_TAG
    } tag_;

#if DEBUG
    // Required by CheckAllElementsAreUnique().
    friend struct ConstantArraySlice;
#endif
  };

  index_t AllocateIndex(Entry constant_entry);
  index_t AllocateIndexArray(Entry constant_entry, size_t size);
  index_t AllocateReservedEntry(Tagged<Smi> value);

  struct ConstantArraySlice final : public ZoneObject {
    ConstantArraySlice(Zone* zone, size_t start_index, size_t capacity,
                       OperandSize operand_size);
    ConstantArraySlice(const ConstantArraySlice&) = delete;
    ConstantArraySlice& operator=(const ConstantArraySlice&) = delete;

    void Reserve();
    void Unreserve();
    size_t Allocate(Entry entry, size_t count = 1);
    Entry& At(size_t index);
    const Entry& At(size_t index) const;

#if DEBUG
    template <typename IsolateT>
    void CheckAllElementsAreUnique(IsolateT* isolate) const;
#endif

    inline size_t available() const { return capacity() - reserved() - size(); }
    inline size_t reserved() const { return reserved_; }
    inline size_t capacity() const { return capacity_; }
    inline size_t size() const { return constants_.size(); }
    inline size_t start_index() const { return start_index_; }
    inline size_t max_index() const { return start_index_ + capacity() - 1; }
    inline OperandSize operand_size() const { return operand_size_; }

   private:
    const size_t start_index_;
    const size_t capacity_;
    size_t reserved_;
    OperandSize operand_size_;
    ZoneVector<Entry> constants_;
  };

  ConstantArraySlice* IndexToSlice(size_t index) const;
  ConstantArraySlice* OperandSizeToSlice(OperandSize operand_size) const;

  ConstantArraySlice* idx_slice_[3];
  base::TemplateHashMapImpl<intptr_t, index_t,
                            base::KeyEqualityMatcher<intptr_t>,
                            ZoneAllocationPolicy>
      constants_map_;
  ZoneMap<Tagged<Smi>, index_t> smi_map_;
  ZoneVector<std::pair<Tagged<Smi>, index_t>> smi_pairs_;
  ZoneMap<double, index_t> heap_number_map_;

#define SINGLETON_ENTRY_FIELD(NAME, LOWER_NAME) int LOWER_NAME##_ = -1;
  SINGLETON_CONSTANT_ENTRY_TYPES(SINGLETON_ENTRY_FIELD)
#undef SINGLETON_ENTRY_FIELD
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_CONSTANT_ARRAY_BUILDER_H_

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_STRUCT_TYPES_H_
#define V8_WASM_STRUCT_TYPES_H_

#include "src/base/iterator.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/wasm/value-type.h"
#include "src/zone/zone.h"

namespace v8::internal::wasm {

class StructTypeBase : public ZoneObject {
 public:
  StructTypeBase(uint32_t field_count, uint32_t* field_offsets,
                 const ValueTypeBase* reps, const bool* mutabilities)
      : field_count_(field_count),
        field_offsets_(field_offsets),
        reps_(reps),
        mutabilities_(mutabilities) {}

  uint32_t field_count() const { return field_count_; }

  ValueTypeBase field(uint32_t index) const {
    DCHECK_LT(index, field_count_);
    return reps_[index];
  }

  bool mutability(uint32_t index) const {
    DCHECK_LT(index, field_count_);
    return mutabilities_[index];
  }

  // Iteration support.
  base::iterator_range<const ValueTypeBase*> fields() const {
    return {reps_, reps_ + field_count_};
  }
  base::iterator_range<const bool*> mutabilities() const {
    return {mutabilities_, mutabilities_ + field_count_};
  }

  // Returns the offset of this field in the runtime representation of the
  // object, from the start of the object fields (disregarding the object
  // header).
  uint32_t field_offset(uint32_t index) const {
    DCHECK_LT(index, field_count());
    if (index == 0) return 0;
    DCHECK(offsets_initialized_);
    return field_offsets_[index - 1];
  }
  uint32_t total_fields_size() const {
    return field_count() == 0 ? 0 : field_offsets_[field_count() - 1];
  }

  uint32_t Align(uint32_t offset, uint32_t alignment) {
    return RoundUp(offset, std::min(alignment, uint32_t{kTaggedSize}));
  }

  void InitializeOffsets() {
    if (field_count() == 0) return;
    DCHECK(!offsets_initialized_);
    uint32_t offset = field(0).value_kind_size();
    // Optimization: we track the last gap that was introduced by alignment,
    // and place any sufficiently-small fields in it.
    // It's important that the algorithm that assigns offsets to fields is
    // subtyping-safe, i.e. two lists of fields with a common prefix must
    // always compute the same offsets for the fields in this common prefix.
    uint32_t gap_position = 0;
    uint32_t gap_size = 0;
    for (uint32_t i = 1; i < field_count(); i++) {
      uint32_t field_size = field(i).value_kind_size();
      if (field_size <= gap_size) {
        uint32_t aligned_gap = Align(gap_position, field_size);
        uint32_t gap_before = aligned_gap - gap_position;
        uint32_t aligned_gap_size = gap_size - gap_before;
        if (field_size <= aligned_gap_size) {
          field_offsets_[i - 1] = aligned_gap;
          uint32_t gap_after = aligned_gap_size - field_size;
          if (gap_before > gap_after) {
            // Keep old {gap_position}.
            gap_size = gap_before;
          } else {
            gap_position = aligned_gap + field_size;
            gap_size = gap_after;
          }
          continue;  // Successfully placed the field in the gap.
        }
      }
      uint32_t old_offset = offset;
      offset = Align(offset, field_size);
      uint32_t gap = offset - old_offset;
      if (gap > gap_size) {
        gap_size = gap;
        gap_position = old_offset;
      }
      field_offsets_[i - 1] = offset;
      offset += field_size;
    }
    offset = RoundUp(offset, kTaggedSize);
    field_offsets_[field_count() - 1] = offset;
#if DEBUG
    offsets_initialized_ = true;
#endif
  }

  // For incrementally building StructTypes.
  template <class Subclass, class ValueTypeSubclass>
  class BuilderImpl {
   public:
    enum ComputeOffsets : bool {
      kComputeOffsets = true,
      kUseProvidedOffsets = false
    };

    BuilderImpl(Zone* zone, uint32_t field_count)
        : zone_(zone),
          field_count_(field_count),
          cursor_(0),
          field_offsets_(zone_->AllocateArray<uint32_t>(field_count_)),
          buffer_(zone->AllocateArray<ValueTypeSubclass>(
              static_cast<int>(field_count))),
          mutabilities_(
              zone->AllocateArray<bool>(static_cast<int>(field_count))) {}

    void AddField(ValueTypeSubclass type, bool mutability,
                  uint32_t offset = 0) {
      DCHECK_LT(cursor_, field_count_);
      if (cursor_ > 0) {
        field_offsets_[cursor_ - 1] = offset;
      } else {
        DCHECK_EQ(0, offset);  // First field always has offset 0.
      }
      mutabilities_[cursor_] = mutability;
      buffer_[cursor_++] = type;
    }

    void set_total_fields_size(uint32_t size) {
      if (field_count_ == 0) {
        DCHECK_EQ(0, size);
        return;
      }
      field_offsets_[field_count_ - 1] = size;
    }

    Subclass* Build(ComputeOffsets compute_offsets = kComputeOffsets) {
      DCHECK_EQ(cursor_, field_count_);
      Subclass* result = zone_->New<Subclass>(field_count_, field_offsets_,
                                              buffer_, mutabilities_);
      if (compute_offsets == kComputeOffsets) {
        result->InitializeOffsets();
      } else {
#if DEBUG
        bool offsets_specified = true;
        for (uint32_t i = 0; i < field_count_; i++) {
          if (field_offsets_[i] == 0) {
            offsets_specified = false;
            break;
          }
        }
        result->offsets_initialized_ = offsets_specified;
#endif
      }
      return result;
    }

   private:
    Zone* const zone_;
    const uint32_t field_count_;
    uint32_t cursor_;
    uint32_t* field_offsets_;
    ValueTypeSubclass* const buffer_;
    bool* const mutabilities_;
  };

  static const size_t kMaxFieldOffset =
      (kV8MaxWasmStructFields - 1) * kMaxValueTypeSize;

 private:
  friend class StructType;
  friend class CanonicalStructType;

  const uint32_t field_count_;
#if DEBUG
  bool offsets_initialized_ = false;
#endif
  uint32_t* const field_offsets_;
  const ValueTypeBase* const reps_;
  const bool* const mutabilities_;
};

// Module-relative type indices.
class StructType : public StructTypeBase {
 public:
  using Builder = StructTypeBase::BuilderImpl<StructType, ValueType>;

  StructType(uint32_t field_count, uint32_t* field_offsets,
             const ValueType* reps, const bool* mutabilities)
      : StructTypeBase(field_count, field_offsets, reps, mutabilities) {}

  bool operator==(const StructType& other) const {
    if (this == &other) return true;
    if (field_count() != other.field_count()) return false;
    return std::equal(fields().begin(), fields().end(),
                      other.fields().begin()) &&
           std::equal(mutabilities().begin(), mutabilities().end(),
                      other.mutabilities().begin());
  }
  bool operator!=(const StructType& other) const { return !(*this == other); }

  ValueType field(uint32_t index) const {
    return ValueType{StructTypeBase::field(index)};
  }

  base::iterator_range<const ValueType*> fields() const {
    const ValueType* cast_reps = static_cast<const ValueType*>(reps_);
    return {cast_reps, cast_reps + field_count_};
  }
};

// Canonicalized type indices.
class CanonicalStructType : public StructTypeBase {
 public:
  using Builder =
      StructTypeBase::BuilderImpl<CanonicalStructType, CanonicalValueType>;

  CanonicalStructType(uint32_t field_count, uint32_t* field_offsets,
                      const CanonicalValueType* reps, const bool* mutabilities)
      : StructTypeBase(field_count, field_offsets, reps, mutabilities) {}

  CanonicalValueType field(uint32_t index) const {
    return CanonicalValueType{StructTypeBase::field(index)};
  }

  bool operator==(const CanonicalStructType& other) const {
    if (this == &other) return true;
    if (field_count() != other.field_count()) return false;
    return std::equal(fields().begin(), fields().end(),
                      other.fields().begin()) &&
           std::equal(mutabilities().begin(), mutabilities().end(),
                      other.mutabilities().begin());
  }
  bool operator!=(const CanonicalStructType& other) const {
    return !(*this == other);
  }

  base::iterator_range<const CanonicalValueType*> fields() const {
    const CanonicalValueType* cast_reps =
        static_cast<const CanonicalValueType*>(reps_);
    return {cast_reps, cast_reps + field_count_};
  }
};

inline std::ostream& operator<<(std::ostream& out, StructTypeBase type) {
  out << "[";
  for (ValueTypeBase field : type.fields()) {
    out << field.name() << ", ";
  }
  out << "]";
  return out;
}

class ArrayTypeBase : public ZoneObject {
 public:
  constexpr explicit ArrayTypeBase(bool mutability) : mutability_(mutability) {}

  bool mutability() const { return mutability_; }

 protected:
  const bool mutability_;
};

class ArrayType : public ArrayTypeBase {
 public:
  constexpr ArrayType(ValueType rep, bool mutability)
      : ArrayTypeBase(mutability), rep_(rep) {}

  bool operator==(const ArrayType& other) const {
    return rep_ == other.rep_ && mutability_ == other.mutability_;
  }
  bool operator!=(const ArrayType& other) const {
    return rep_ != other.rep_ || mutability_ != other.mutability_;
  }

  ValueType element_type() const { return rep_; }

 private:
  ValueType rep_;
};

class CanonicalArrayType : public ArrayTypeBase {
 public:
  CanonicalArrayType(CanonicalValueType rep, bool mutability)
      : ArrayTypeBase(mutability), rep_(rep) {}

  bool operator==(const CanonicalArrayType& other) const {
    return rep_ == other.rep_ && mutability_ == other.mutability_;
  }
  bool operator!=(const CanonicalArrayType& other) const {
    return rep_ != other.rep_ || mutability_ != other.mutability_;
  }

  CanonicalValueType element_type() const { return rep_; }

 private:
  CanonicalValueType rep_;
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_STRUCT_TYPES_H_

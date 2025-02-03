// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_NAME_H_
#define V8_OBJECTS_NAME_H_

#include <atomic>

#include "src/base/bit-field.h"
#include "src/objects/objects.h"
#include "src/objects/primitive-heap-object.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

namespace compiler {
class WasmGraphBuilder;
}

class SharedStringAccessGuardIfNeeded;

// The Name abstract class captures anything that can be used as a property
// name, i.e., strings and symbols.  All names store a hash value.
V8_OBJECT class Name : public PrimitiveHeapObject {
 public:
  // Tells whether the hash code has been computed.
  // Note: Use TryGetHash() whenever you want to use the hash, instead of a
  // combination of HashHashCode() and hash() for thread-safety.
  inline bool HasHashCode() const;
  // Tells whether the name contains a forwarding index pointing to a row
  // in the string forwarding table.
  inline bool HasForwardingIndex(AcquireLoadTag) const;
  inline bool HasInternalizedForwardingIndex(AcquireLoadTag) const;
  inline bool HasExternalForwardingIndex(AcquireLoadTag) const;

  inline uint32_t raw_hash_field() const {
    return raw_hash_field_.load(std::memory_order_relaxed);
  }

  inline uint32_t raw_hash_field(AcquireLoadTag) const {
    return raw_hash_field_.load(std::memory_order_acquire);
  }

  inline void set_raw_hash_field(uint32_t hash) {
    raw_hash_field_.store(hash, std::memory_order_relaxed);
  }

  inline void set_raw_hash_field(uint32_t hash, ReleaseStoreTag) {
    raw_hash_field_.store(hash, std::memory_order_release);
  }

  // Sets the hash field only if it is empty. Otherwise does nothing.
  inline void set_raw_hash_field_if_empty(uint32_t hash);

  // Returns a hash value used for the property table (same as Hash()), assumes
  // the hash is already computed.
  inline uint32_t hash() const;

  // Returns true if the hash has been computed, and sets the computed hash
  // as out-parameter.
  inline bool TryGetHash(uint32_t* hash) const;

  // Equality operations.
  inline bool Equals(Tagged<Name> other);
  inline static bool Equals(Isolate* isolate, Handle<Name> one,
                            Handle<Name> two);

  // Conversion.
  inline bool AsArrayIndex(uint32_t* index);
  inline bool AsIntegerIndex(size_t* index);

  // An "interesting" is a well-known symbol or string, like @@toStringTag,
  // @@toJSON, that's often looked up on random objects but is usually not
  // present. We optimize this by setting a flag on the object's map when such
  // symbol properties are added, so we can optimize lookups on objects
  // that don't have the flag.
  inline bool IsInteresting(Isolate* isolate);

  // If the name is private, it can only name own properties.
  inline bool IsPrivate();

  // If the name is a private name, it should behave like a private
  // symbol but also throw on property access miss.
  inline bool IsPrivateName();

  // If the name is a private brand, it should behave like a private name
  // symbol but is filtered out when generating list of private fields.
  inline bool IsPrivateBrand();

  static inline bool ContainsCachedArrayIndex(uint32_t hash);

  // Return a string version of this name that is converted according to the
  // rules described in ES6 section 9.2.11.
  V8_WARN_UNUSED_RESULT static MaybeHandle<String> ToFunctionName(
      Isolate* isolate, Handle<Name> name);
  V8_WARN_UNUSED_RESULT static MaybeHandle<String> ToFunctionName(
      Isolate* isolate, Handle<Name> name, DirectHandle<String> prefix);

  DECL_VERIFIER(Name)
  DECL_PRINTER(Name)
  void NameShortPrint();
  int NameShortPrint(base::Vector<char> str);

  // Mask constant for checking if a name has a computed hash code and the type
  // of information stored in the hash field. The least significant bit
  // indicates whether the value can be used as a hash (i.e. different values
  // imply different strings).
  enum class HashFieldType : uint32_t {
    kHash = 0b10,
    kIntegerIndex = 0b00,
    kForwardingIndex = 0b01,
    kEmpty = 0b11
  };

  using HashFieldTypeBits = base::BitField<HashFieldType, 0, 2>;
  using HashBits =
      HashFieldTypeBits::Next<uint32_t, kBitsPerInt - HashFieldTypeBits::kSize>;

  static constexpr int kHashNotComputedMask = 1;
  // Value of empty hash field indicating that the hash is not computed.
  static constexpr int kEmptyHashField =
      HashFieldTypeBits::encode(HashFieldType::kEmpty);

  // Empty hash and forwarding indices can not be used as hash.
  static_assert((kEmptyHashField & kHashNotComputedMask) != 0);
  static_assert((HashFieldTypeBits::encode(HashFieldType::kForwardingIndex) &
                 kHashNotComputedMask) != 0);

  using IsInternalizedForwardingIndexBit = HashFieldTypeBits::Next<bool, 1>;
  using IsExternalForwardingIndexBit =
      IsInternalizedForwardingIndexBit::Next<bool, 1>;
  using ForwardingIndexValueBits = IsExternalForwardingIndexBit::Next<
      unsigned int, kBitsPerInt - HashFieldTypeBits::kSize -
                        IsInternalizedForwardingIndexBit::kSize -
                        IsExternalForwardingIndexBit::kSize>;

  // Array index strings this short can keep their index in the hash field.
  static const int kMaxCachedArrayIndexLength = 7;

  // Maximum number of characters to consider when trying to convert a string
  // value into an array index.
  static const int kMaxArrayIndexSize = 10;
  // Maximum number of characters in a string that can possibly be an
  // "integer index" in the spec sense, i.e. a canonical representation of a
  // number in the range up to MAX_SAFE_INTEGER. We parse these into a size_t,
  // so the size of that type also factors in as a limit: 10 characters per
  // 32 bits of size_t width.
  static const int kMaxIntegerIndexSize =
      std::min(16, int{10 * (sizeof(size_t) / 4)});

  // For strings which are array indexes the hash value has the string length
  // mixed into the hash, mainly to avoid a hash value of zero which would be
  // the case for the string '0'. 24 bits are used for the array index value.
  static const int kArrayIndexValueBits = 24;
  static const int kArrayIndexLengthBits =
      kBitsPerInt - kArrayIndexValueBits - HashFieldTypeBits::kSize;

  static_assert(kArrayIndexLengthBits > 0);
  static_assert(kMaxArrayIndexSize < (1 << kArrayIndexLengthBits));

  using ArrayIndexValueBits =
      HashFieldTypeBits::Next<unsigned int, kArrayIndexValueBits>;
  using ArrayIndexLengthBits =
      ArrayIndexValueBits::Next<unsigned int, kArrayIndexLengthBits>;

  // Check that kMaxCachedArrayIndexLength + 1 is a power of two so we
  // could use a mask to test if the length of string is less than or equal to
  // kMaxCachedArrayIndexLength.
  static_assert(base::bits::IsPowerOfTwo(kMaxCachedArrayIndexLength + 1),
                "(kMaxCachedArrayIndexLength + 1) must be power of two");

  // When any of these bits is set then the hash field does not contain a cached
  // array index.
  static_assert(HashFieldTypeBits::encode(HashFieldType::kIntegerIndex) == 0);
  static const unsigned int kDoesNotContainCachedArrayIndexMask =
      (~static_cast<unsigned>(kMaxCachedArrayIndexLength)
       << ArrayIndexLengthBits::kShift) |
      HashFieldTypeBits::kMask;

  // When any of these bits is set then the hash field does not contain an
  // integer or forwarding index.
  static const unsigned int kDoesNotContainIntegerOrForwardingIndexMask = 0b10;
  static_assert((HashFieldTypeBits::encode(HashFieldType::kIntegerIndex) &
                 kDoesNotContainIntegerOrForwardingIndexMask) == 0);
  static_assert((HashFieldTypeBits::encode(HashFieldType::kForwardingIndex) &
                 kDoesNotContainIntegerOrForwardingIndexMask) == 0);

  // Returns a hash value used for the property table. Ensures that the hash
  // value is computed.
  //
  // The overload without SharedStringAccessGuardIfNeeded can only be called on
  // the main thread.
  inline uint32_t EnsureHash();
  inline uint32_t EnsureHash(const SharedStringAccessGuardIfNeeded&);
  // The value returned is always a computed hash, even if the value stored is
  // a forwarding index.
  inline uint32_t EnsureRawHash();
  inline uint32_t EnsureRawHash(const SharedStringAccessGuardIfNeeded&);
  inline uint32_t RawHash();

  static inline bool IsHashFieldComputed(uint32_t raw_hash_field);
  static inline bool IsHash(uint32_t raw_hash_field);
  static inline bool IsIntegerIndex(uint32_t raw_hash_field);
  static inline bool IsForwardingIndex(uint32_t raw_hash_field);
  static inline bool IsInternalizedForwardingIndex(uint32_t raw_hash_field);
  static inline bool IsExternalForwardingIndex(uint32_t raw_hash_field);

  static inline uint32_t CreateHashFieldValue(uint32_t hash,
                                              HashFieldType type);
  static inline uint32_t CreateInternalizedForwardingIndex(uint32_t index);
  static inline uint32_t CreateExternalForwardingIndex(uint32_t index);

 private:
  friend class V8HeapExplorer;
  friend class CodeStubAssembler;
  friend class StringBuiltinsAssembler;
  friend class maglev::MaglevAssembler;
  friend class compiler::AccessBuilder;
  friend class compiler::WasmGraphBuilder;
  friend class TorqueGeneratedNameAsserts;

  inline uint32_t GetRawHashFromForwardingTable(uint32_t raw_hash) const;

  std::atomic_uint32_t raw_hash_field_;
} V8_OBJECT_END;

inline bool IsUniqueName(Tagged<Name> obj);
inline bool IsUniqueName(Tagged<Name> obj, PtrComprCageBase cage_base);

// ES6 symbols.
V8_OBJECT class Symbol : public Name {
 public:
  using IsPrivateBit = base::BitField<bool, 0, 1>;
  using IsWellKnownSymbolBit = IsPrivateBit::Next<bool, 1>;
  using IsInPublicSymbolTableBit = IsWellKnownSymbolBit::Next<bool, 1>;
  using IsInterestingSymbolBit = IsInPublicSymbolTableBit::Next<bool, 1>;
  using IsPrivateNameBit = IsInterestingSymbolBit::Next<bool, 1>;
  using IsPrivateBrandBit = IsPrivateNameBit::Next<bool, 1>;

  inline Tagged<PrimitiveHeapObject> description() const;
  inline void set_description(Tagged<PrimitiveHeapObject> value,
                              WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // [is_private]: Whether this is a private symbol.  Private symbols can only
  // be used to designate own properties of objects.
  inline bool is_private() const;
  inline void set_is_private(bool value);

  // [is_well_known_symbol]: Whether this is a spec-defined well-known symbol,
  // or not. Well-known symbols do not throw when an access check fails during
  // a load.
  inline bool is_well_known_symbol() const;
  inline void set_is_well_known_symbol(bool value);

  // [is_interesting_symbol]: Whether this is an "interesting symbol", which
  // is a well-known symbol like @@toStringTag that's often looked up on
  // random objects but is usually not present. See Name::IsInterestingSymbol()
  // for a detailed description.
  inline bool is_interesting_symbol() const;
  inline void set_is_interesting_symbol(bool value);

  // [is_in_public_symbol_table]: Whether this is a symbol created by
  // Symbol.for. Calling Symbol.keyFor on such a symbol simply needs
  // to return the attached name.
  inline bool is_in_public_symbol_table() const;
  inline void set_is_in_public_symbol_table(bool value);

  // [is_private_name]: Whether this is a private name.  Private names
  // are the same as private symbols except they throw on missing
  // property access.
  //
  // This also sets the is_private bit.
  inline bool is_private_name() const;
  inline void set_is_private_name();

  // [is_private_name]: Whether this is a brand symbol.  Brand symbols are
  // private name symbols that are used for validating access to
  // private methods and storing information about the private methods.
  //
  // This also sets the is_private bit.
  inline bool is_private_brand() const;
  inline void set_is_private_brand();

  // Dispatched behavior.
  DECL_PRINTER(Symbol)
  DECL_VERIFIER(Symbol)

  void SymbolShortPrint(std::ostream& os);

 private:
  friend class Factory;
  friend struct ObjectTraits<Symbol>;
  friend struct OffsetsForDebug;
  friend class V8HeapExplorer;
  friend class CodeStubAssembler;
  friend class maglev::MaglevAssembler;
  friend class TorqueGeneratedSymbolAsserts;

  // TODO(cbruni): remove once the new maptracer is in place.
  friend class Name;  // For PrivateSymbolToName.

  uint32_t flags() const { return flags_; }
  void set_flags(uint32_t value) { flags_ = value; }

  const char* PrivateSymbolToName() const;

  uint32_t flags_;
  // String|Undefined
  // TODO(leszeks): Introduce a union type for this.
  TaggedMember<PrimitiveHeapObject> description_;
} V8_OBJECT_END;

template <>
struct ObjectTraits<Symbol> {
  using BodyDescriptor = FixedBodyDescriptor<offsetof(Symbol, description_),
                                             sizeof(Symbol), sizeof(Symbol)>;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_NAME_H_

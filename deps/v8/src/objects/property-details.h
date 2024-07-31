// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROPERTY_DETAILS_H_
#define V8_OBJECTS_PROPERTY_DETAILS_H_

#include "include/v8-object.h"
#include "src/base/bit-field.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// ES6 6.1.7.1
enum PropertyAttributes {
  NONE = ::v8::None,
  READ_ONLY = ::v8::ReadOnly,
  DONT_ENUM = ::v8::DontEnum,
  DONT_DELETE = ::v8::DontDelete,

  ALL_ATTRIBUTES_MASK = READ_ONLY | DONT_ENUM | DONT_DELETE,

  SEALED = DONT_DELETE,
  FROZEN = SEALED | READ_ONLY,

  ABSENT = 64,  // Used in runtime to indicate a property is absent.
  // ABSENT can never be stored in or returned from a descriptor's attributes
  // bitfield.  It is only used as a return value meaning the attributes of
  // a non-existent property.
};

V8_INLINE PropertyAttributes PropertyAttributesFromInt(int value) {
  DCHECK_EQ(value & ~PropertyAttributes::ALL_ATTRIBUTES_MASK, 0);
  return static_cast<PropertyAttributes>(value);
}

// Number of distinct bits in PropertyAttributes.
static const int kPropertyAttributesBitsCount = 3;

static const int kPropertyAttributesCombinationsCount =
    1 << kPropertyAttributesBitsCount;

enum PropertyFilter {
  ALL_PROPERTIES = 0,
  ONLY_WRITABLE = 1,
  ONLY_ENUMERABLE = 2,
  ONLY_CONFIGURABLE = 4,
  SKIP_STRINGS = 8,
  SKIP_SYMBOLS = 16,
  PRIVATE_NAMES_ONLY = 32,
  ENUMERABLE_STRINGS = ONLY_ENUMERABLE | SKIP_SYMBOLS,
};
// Enable fast comparisons of PropertyAttributes against PropertyFilters.
static_assert(ALL_PROPERTIES == static_cast<PropertyFilter>(NONE));
static_assert(ONLY_WRITABLE == static_cast<PropertyFilter>(READ_ONLY));
static_assert(ONLY_ENUMERABLE == static_cast<PropertyFilter>(DONT_ENUM));
static_assert(ONLY_CONFIGURABLE == static_cast<PropertyFilter>(DONT_DELETE));
static_assert(((SKIP_STRINGS | SKIP_SYMBOLS) & ALL_ATTRIBUTES_MASK) == 0);
static_assert(ALL_PROPERTIES ==
              static_cast<PropertyFilter>(v8::PropertyFilter::ALL_PROPERTIES));
static_assert(ONLY_WRITABLE ==
              static_cast<PropertyFilter>(v8::PropertyFilter::ONLY_WRITABLE));
static_assert(ONLY_ENUMERABLE ==
              static_cast<PropertyFilter>(v8::PropertyFilter::ONLY_ENUMERABLE));
static_assert(ONLY_CONFIGURABLE == static_cast<PropertyFilter>(
                                       v8::PropertyFilter::ONLY_CONFIGURABLE));
static_assert(SKIP_STRINGS ==
              static_cast<PropertyFilter>(v8::PropertyFilter::SKIP_STRINGS));
static_assert(SKIP_SYMBOLS ==
              static_cast<PropertyFilter>(v8::PropertyFilter::SKIP_SYMBOLS));

// Assert that kPropertyAttributesBitsCount value matches the definition of
// ALL_ATTRIBUTES_MASK.
static_assert((ALL_ATTRIBUTES_MASK == (READ_ONLY | DONT_ENUM | DONT_DELETE)) ==
              (kPropertyAttributesBitsCount == 3));

class Smi;
class TypeInfo;

// Order of kinds is significant.
// Must fit in the BitField PropertyDetails::KindField.
enum class PropertyKind { kData = 0, kAccessor = 1 };

// Order of modes is significant.
// Must fit in the BitField PropertyDetails::LocationField.
enum class PropertyLocation { kField = 0, kDescriptor = 1 };

// Order of modes is significant.
// Must fit in the BitField PropertyDetails::ConstnessField.
enum class PropertyConstness { kMutable = 0, kConst = 1 };

class Representation {
 public:
  enum Kind {
    kNone,
    kSmi,
    kDouble,
    kHeapObject,
    kTagged,
    // This representation is used for WasmObject fields and basically means
    // that the actual field type information must be taken from the Wasm RTT
    // associated with the map.
    kWasmValue,
    kNumRepresentations
  };

  constexpr Representation() : kind_(kNone) {}

  static constexpr Representation None() { return Representation(kNone); }
  static constexpr Representation Tagged() { return Representation(kTagged); }
  static constexpr Representation Smi() { return Representation(kSmi); }
  static constexpr Representation Double() { return Representation(kDouble); }
  static constexpr Representation HeapObject() {
    return Representation(kHeapObject);
  }
  static constexpr Representation WasmValue() {
    return Representation(kWasmValue);
  }

  static constexpr Representation FromKind(Kind kind) {
    return Representation(kind);
  }

  bool Equals(const Representation& other) const {
    return kind_ == other.kind_;
  }

  bool IsCompatibleForLoad(const Representation& other) const {
    return IsDouble() == other.IsDouble();
  }

  bool IsCompatibleForStore(const Representation& other) const {
    return Equals(other);
  }

  // Returns true if a change from this representation to a more general one
  // might cause a map deprecation.
  bool MightCauseMapDeprecation() const {
    // HeapObject to tagged representation change can be done in-place.
    // Boxed double to tagged transition is always done in-place.
    // Note that WasmValue is not supposed to be changed at all (the only
    // representation it fits into is WasmValue), so for the sake of predicate
    // correctness we treat it as in-place "changeable".
    if (IsTagged() || IsHeapObject() || IsDouble() || IsWasmValue()) {
      return false;
    }
    // None to double and smi to double representation changes require
    // deprecation, because doubles might require box allocation, see
    // CanBeInPlaceChangedTo().
    DCHECK(IsNone() || IsSmi());
    return true;
  }

  bool CanBeInPlaceChangedTo(const Representation& other) const {
    if (Equals(other)) return true;
    if (IsWasmValue() || other.IsWasmValue()) return false;
    // If it's just a representation generalization case (i.e. property kind and
    // attributes stays unchanged) it's fine to transition from None to anything
    // but double without any modification to the object, because the default
    // uninitialized value for representation None can be overwritten by both
    // smi and tagged values. Doubles, however, would require a box allocation.
    if (IsNone()) return !other.IsDouble();
    if (!other.IsTagged()) return false;
    DCHECK(IsSmi() || IsDouble() || IsHeapObject());
    return true;
  }

  // Return the most generic representation that this representation can be
  // changed to in-place. If an in-place representation change is not allowed,
  // then this will return the current representation.
  Representation MostGenericInPlaceChange() const {
    if (IsWasmValue()) return Representation::WasmValue();
    return Representation::Tagged();
  }

  bool is_more_general_than(const Representation& other) const {
    if (IsWasmValue()) return false;
    if (IsHeapObject()) return other.IsNone();
    return kind_ > other.kind_;
  }

  bool fits_into(const Representation& other) const {
    return other.is_more_general_than(*this) || other.Equals(*this);
  }

  Representation generalize(Representation other) {
    if (other.fits_into(*this)) return *this;
    if (other.is_more_general_than(*this)) return other;
    return Representation::Tagged();
  }

  int size() const {
    DCHECK(!IsNone());
    if (IsDouble()) return kDoubleSize;
    DCHECK(IsTagged() || IsSmi() || IsHeapObject());
    return kTaggedSize;
  }

  constexpr Kind kind() const { return static_cast<Kind>(kind_); }
  constexpr bool IsNone() const { return kind_ == kNone; }
  constexpr bool IsWasmValue() const { return kind_ == kWasmValue; }
  constexpr bool IsTagged() const { return kind_ == kTagged; }
  constexpr bool IsSmi() const { return kind_ == kSmi; }
  constexpr bool IsSmiOrTagged() const { return IsSmi() || IsTagged(); }
  constexpr bool IsDouble() const { return kind_ == kDouble; }
  constexpr bool IsHeapObject() const { return kind_ == kHeapObject; }

  const char* Mnemonic() const {
    switch (kind_) {
      case kNone:
        return "v";
      case kTagged:
        return "t";
      case kSmi:
        return "s";
      case kDouble:
        return "d";
      case kHeapObject:
        return "h";
      case kWasmValue:
        return "w";
    }
    UNREACHABLE();
  }

  bool operator==(const Representation& other) const {
    return kind_ == other.kind_;
  }

 private:
  explicit constexpr Representation(Kind k) : kind_(k) {}

  // Make sure kind fits in int8.
  static_assert(kNumRepresentations <= (1 << kBitsPerByte));

  int8_t kind_;
};

static const int kDescriptorIndexBitCount = 10;
static const int kFirstInobjectPropertyOffsetBitCount = 7;
// The maximum number of descriptors we want in a descriptor array.  It should
// fit in a page and also the following should hold:
// kMaxNumberOfDescriptors + kFieldsAdded <= PropertyArray::kMaxLength.
static const int kMaxNumberOfDescriptors = (1 << kDescriptorIndexBitCount) - 4;
static const int kInvalidEnumCacheSentinel =
    (1 << kDescriptorIndexBitCount) - 1;

// A PropertyCell's property details contains a cell type that is meaningful if
// the cell is still valid (does not hold the hole).
enum class PropertyCellType {
  kMutable,       // Cell will no longer be tracked as constant.
  kUndefined,     // The PREMONOMORPHIC of property cells.
  kConstant,      // Cell has been assigned only once.
  kConstantType,  // Cell has been assigned only one type.
  // Temporary value indicating an ongoing property cell state transition. Only
  // observable by a background thread.
  kInTransition,
  // Value for dictionaries not holding cells, must be 0:
  kNoCell = kMutable,
};

// PropertyDetails captures type and attributes for a property.
// They are used both in property dictionaries and instance descriptors.
class PropertyDetails {
 public:
  // Property details for global dictionary properties.
  constexpr PropertyDetails(PropertyKind kind, PropertyAttributes attributes,
                            PropertyCellType cell_type,
                            int dictionary_index = 0)
      : value_(KindField::encode(kind) |
               LocationField::encode(PropertyLocation::kField) |
               AttributesField::encode(attributes) |
               // We track PropertyCell constness via PropertyCellTypeField,
               // so we set ConstnessField to kMutable to simplify DCHECKs
               // related to non-global property constness tracking.
               ConstnessField::encode(PropertyConstness::kMutable) |
               DictionaryStorageField::encode(dictionary_index) |
               PropertyCellTypeField::encode(cell_type)) {}

  // Property details for dictionary mode properties/elements.
  constexpr PropertyDetails(PropertyKind kind, PropertyAttributes attributes,
                            PropertyConstness constness,
                            int dictionary_index = 0)
      : value_(KindField::encode(kind) |
               LocationField::encode(PropertyLocation::kField) |
               AttributesField::encode(attributes) |
               ConstnessField::encode(constness) |
               DictionaryStorageField::encode(dictionary_index) |
               PropertyCellTypeField::encode(PropertyCellType::kNoCell)) {}

  // Property details for fast mode properties.
  constexpr PropertyDetails(PropertyKind kind, PropertyAttributes attributes,
                            PropertyLocation location,
                            PropertyConstness constness,
                            Representation representation, int field_index = 0)
      : value_(
            KindField::encode(kind) | AttributesField::encode(attributes) |
            LocationField::encode(location) |
            ConstnessField::encode(constness) |
            RepresentationField::encode(EncodeRepresentation(representation)) |
            FieldIndexField::encode(field_index)) {}

  static constexpr PropertyDetails Empty(
      PropertyCellType cell_type = PropertyCellType::kNoCell) {
    return PropertyDetails(PropertyKind::kData, NONE, cell_type);
  }

  bool operator==(PropertyDetails const& other) const {
    return value_ == other.value_;
  }

  bool operator!=(PropertyDetails const& other) const {
    return value_ != other.value_;
  }

  int pointer() const { return DescriptorPointer::decode(value_); }

  PropertyDetails set_pointer(int i) const {
    return PropertyDetails(value_, i);
  }

  PropertyDetails set_cell_type(PropertyCellType type) const {
    PropertyDetails details = *this;
    details.value_ = PropertyCellTypeField::update(details.value_, type);
    return details;
  }

  PropertyDetails set_index(int index) const {
    PropertyDetails details = *this;
    details.value_ = DictionaryStorageField::update(details.value_, index);
    return details;
  }

  PropertyDetails CopyWithRepresentation(Representation representation) const {
    return PropertyDetails(value_, representation);
  }
  PropertyDetails CopyWithConstness(PropertyConstness constness) const {
    return PropertyDetails(value_, constness);
  }
  PropertyDetails CopyAddAttributes(PropertyAttributes new_attributes) const {
    new_attributes =
        static_cast<PropertyAttributes>(attributes() | new_attributes);
    return PropertyDetails(value_, new_attributes);
  }

  // Conversion for storing details as Object.
  explicit inline PropertyDetails(Tagged<Smi> smi);
  inline Tagged<Smi> AsSmi() const;

  static constexpr uint8_t EncodeRepresentation(Representation representation) {
    return representation.kind();
  }

  static Representation DecodeRepresentation(uint32_t bits) {
    return Representation::FromKind(static_cast<Representation::Kind>(bits));
  }

  PropertyKind kind() const { return KindField::decode(value_); }
  PropertyLocation location() const { return LocationField::decode(value_); }
  PropertyConstness constness() const { return ConstnessField::decode(value_); }

  PropertyAttributes attributes() const {
    return AttributesField::decode(value_);
  }

  bool HasKindAndAttributes(PropertyKind kind, PropertyAttributes attributes) {
    return (value_ & (KindField::kMask | AttributesField::kMask)) ==
           (KindField::encode(kind) | AttributesField::encode(attributes));
  }

  int dictionary_index() const {
    return DictionaryStorageField::decode(value_);
  }

  Representation representation() const {
    return DecodeRepresentation(RepresentationField::decode(value_));
  }

  int field_index() const { return FieldIndexField::decode(value_); }

  inline int field_width_in_words() const;

  static bool IsValidIndex(int index) {
    return DictionaryStorageField::is_valid(index);
  }

  bool IsReadOnly() const { return (attributes() & READ_ONLY) != 0; }
  bool IsConfigurable() const { return (attributes() & DONT_DELETE) == 0; }
  bool IsDontEnum() const { return (attributes() & DONT_ENUM) != 0; }
  bool IsEnumerable() const { return !IsDontEnum(); }
  PropertyCellType cell_type() const {
    return PropertyCellTypeField::decode(value_);
  }

  // Bit fields in value_ (type, shift, size). Must be public so the
  // constants can be embedded in generated code.
  using KindField = base::BitField<PropertyKind, 0, 1>;
  using ConstnessField = KindField::Next<PropertyConstness, 1>;
  using AttributesField = ConstnessField::Next<PropertyAttributes, 3>;
  static const int kAttributesReadOnlyMask =
      (READ_ONLY << AttributesField::kShift);
  static const int kAttributesDontDeleteMask =
      (DONT_DELETE << AttributesField::kShift);
  static const int kAttributesDontEnumMask =
      (DONT_ENUM << AttributesField::kShift);

  // Bit fields for normalized/dictionary mode objects.
  using PropertyCellTypeField = AttributesField::Next<PropertyCellType, 3>;
  using DictionaryStorageField = PropertyCellTypeField::Next<uint32_t, 23>;

  // Bit fields for fast objects.
  using LocationField = AttributesField::Next<PropertyLocation, 1>;
  using RepresentationField = LocationField::Next<uint32_t, 3>;
  using DescriptorPointer =
      RepresentationField::Next<uint32_t, kDescriptorIndexBitCount>;
  using FieldIndexField =
      DescriptorPointer::Next<uint32_t, kDescriptorIndexBitCount>;

  // All bits for both fast and slow objects must fit in a smi.
  static_assert(DictionaryStorageField::kLastUsedBit < 31);
  static_assert(FieldIndexField::kLastUsedBit < 31);

  // DictionaryStorageField must be the last field, so that overflowing it
  // doesn't overwrite other fields.
  static_assert(DictionaryStorageField::kLastUsedBit == 30);

  // All bits for non-global dictionary mode objects except enumeration index
  // must fit in a byte.
  static_assert(KindField::kLastUsedBit < 8);
  static_assert(ConstnessField::kLastUsedBit < 8);
  static_assert(AttributesField::kLastUsedBit < 8);

  static const int kInitialIndex = 1;

  static constexpr PropertyConstness kConstIfDictConstnessTracking =
      V8_DICT_PROPERTY_CONST_TRACKING_BOOL ? PropertyConstness::kConst
                                           : PropertyConstness::kMutable;

#ifdef OBJECT_PRINT
  // For our gdb macros, we should perhaps change these in the future.
  void Print(bool dictionary_mode);
#endif

  enum PrintMode {
    kPrintAttributes = 1 << 0,
    kPrintFieldIndex = 1 << 1,
    kPrintRepresentation = 1 << 2,
    kPrintPointer = 1 << 3,

    kForProperties = kPrintFieldIndex,
    kForTransitions = kPrintAttributes,
    kPrintFull = -1,
  };
  void PrintAsSlowTo(std::ostream& out, bool print_dict_index);
  void PrintAsFastTo(std::ostream& out, PrintMode mode = kPrintFull);

  // Encodes those property details for non-global dictionary properties
  // with an enumeration index of 0 as a single byte.
  uint8_t ToByte() {
    // We only care about the value of KindField, ConstnessField, and
    // AttributesField. We've statically asserted earlier that these fields fit
    // into a byte together.

    DCHECK_EQ(PropertyLocation::kField, location());
    static_assert(static_cast<int>(PropertyLocation::kField) == 0);

    DCHECK_EQ(PropertyCellType::kNoCell, cell_type());
    static_assert(static_cast<int>(PropertyCellType::kNoCell) == 0);

    // Only to be used when the enum index isn't actually maintained
    // by the PropertyDetails:
    DCHECK_EQ(0, dictionary_index());

    return value_;
  }

  // Only to be used for bytes obtained by ToByte. In particular, only used for
  // non-global dictionary properties.
  static PropertyDetails FromByte(uint8_t encoded_details) {
    // The 0-extension to 32bit sets PropertyLocation to kField,
    // PropertyCellType to kNoCell, and enumeration index to 0, as intended.
    // Everything else is obtained from |encoded_details|.
    PropertyDetails details(encoded_details);
    DCHECK_EQ(PropertyLocation::kField, details.location());
    DCHECK_EQ(PropertyCellType::kNoCell, details.cell_type());
    DCHECK_EQ(0, details.dictionary_index());
    return details;
  }

 private:
  PropertyDetails(int value, int pointer) {
    value_ = DescriptorPointer::update(value, pointer);
  }
  PropertyDetails(int value, Representation representation) {
    value_ = RepresentationField::update(value,
                                         EncodeRepresentation(representation));
  }
  PropertyDetails(int value, PropertyConstness constness) {
    value_ = ConstnessField::update(value, constness);
  }
  PropertyDetails(int value, PropertyAttributes attributes) {
    value_ = AttributesField::update(value, attributes);
  }

  explicit PropertyDetails(uint32_t value) : value_{value} {}

  uint32_t value_;
};

// kField location is more general than kDescriptor, kDescriptor generalizes
// only to itself.
inline bool IsGeneralizableTo(PropertyLocation a, PropertyLocation b) {
  return b == PropertyLocation::kField || a == PropertyLocation::kDescriptor;
}

// PropertyConstness::kMutable constness is more general than
// VariableMode::kConst, VariableMode::kConst generalizes only to itself.
inline bool IsGeneralizableTo(PropertyConstness a, PropertyConstness b) {
  return b == PropertyConstness::kMutable || a == PropertyConstness::kConst;
}

inline PropertyConstness GeneralizeConstness(PropertyConstness a,
                                             PropertyConstness b) {
  return a == PropertyConstness::kMutable ? PropertyConstness::kMutable : b;
}

V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, const Representation& representation);
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, const PropertyAttributes& attributes);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           PropertyConstness constness);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           PropertyCellType type);
}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_PROPERTY_DETAILS_H_

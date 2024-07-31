// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/property.h"

#include "src/handles/handles-inl.h"
#include "src/objects/field-type.h"
#include "src/objects/name-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

std::ostream& operator<<(std::ostream& os,
                         const Representation& representation) {
  switch (representation.kind()) {
    case Representation::kNone:
      return os << "none";
    case Representation::kSmi:
      return os << "smi";
    case Representation::kDouble:
      return os << "double";
    case Representation::kHeapObject:
      return os << "heap-object";
    case Representation::kTagged:
      return os << "tagged";
    case Representation::kWasmValue:
      return os << "wasm-value";
    case Representation::kNumRepresentations:
      UNREACHABLE();
  }
  UNREACHABLE();
}

std::ostream& operator<<(std::ostream& os,
                         const PropertyAttributes& attributes) {
  os << "[";
  os << (((attributes & READ_ONLY) == 0) ? "W" : "_");    // writable
  os << (((attributes & DONT_ENUM) == 0) ? "E" : "_");    // enumerable
  os << (((attributes & DONT_DELETE) == 0) ? "C" : "_");  // configurable
  os << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, PropertyConstness constness) {
  switch (constness) {
    case PropertyConstness::kMutable:
      return os << "mutable";
    case PropertyConstness::kConst:
      return os << "const";
  }
  UNREACHABLE();
}

Descriptor::Descriptor() : details_(Smi::zero()) {}

Descriptor::Descriptor(Handle<Name> key, const MaybeObjectHandle& value,
                       PropertyKind kind, PropertyAttributes attributes,
                       PropertyLocation location, PropertyConstness constness,
                       Representation representation, int field_index)
    : key_(key),
      value_(value),
      details_(kind, attributes, location, constness, representation,
               field_index) {
  DCHECK(IsUniqueName(*key));
  DCHECK_IMPLIES(key->IsPrivate(), !details_.IsEnumerable());
}

Descriptor::Descriptor(Handle<Name> key, const MaybeObjectHandle& value,
                       PropertyDetails details)
    : key_(key), value_(value), details_(details) {
  DCHECK(IsUniqueName(*key));
  DCHECK_IMPLIES(key->IsPrivate(), !details_.IsEnumerable());
}

Descriptor Descriptor::DataField(Isolate* isolate, Handle<Name> key,
                                 int field_index, PropertyAttributes attributes,
                                 Representation representation) {
  return DataField(key, field_index, attributes, PropertyConstness::kMutable,
                   representation, MaybeObjectHandle(FieldType::Any(isolate)));
}

Descriptor Descriptor::DataField(Handle<Name> key, int field_index,
                                 PropertyAttributes attributes,
                                 PropertyConstness constness,
                                 Representation representation,
                                 const MaybeObjectHandle& wrapped_field_type) {
  DCHECK(IsSmi(*wrapped_field_type) || IsWeak(*wrapped_field_type));
  PropertyDetails details(PropertyKind::kData, attributes,
                          PropertyLocation::kField, constness, representation,
                          field_index);
  return Descriptor(key, wrapped_field_type, details);
}

Descriptor Descriptor::DataConstant(Handle<Name> key, Handle<Object> value,
                                    PropertyAttributes attributes) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*key);
  return Descriptor(key, MaybeObjectHandle(value), PropertyKind::kData,
                    attributes, PropertyLocation::kDescriptor,
                    PropertyConstness::kConst,
                    Object::OptimalRepresentation(*value, cage_base), 0);
}

Descriptor Descriptor::DataConstant(Isolate* isolate, Handle<Name> key,
                                    int field_index, Handle<Object> value,
                                    PropertyAttributes attributes) {
  MaybeObjectHandle any_type(FieldType::Any(), isolate);
  return DataField(key, field_index, attributes, PropertyConstness::kConst,
                   Representation::Tagged(), any_type);
}

Descriptor Descriptor::AccessorConstant(Handle<Name> key,
                                        Handle<Object> foreign,
                                        PropertyAttributes attributes) {
  return Descriptor(key, MaybeObjectHandle(foreign), PropertyKind::kAccessor,
                    attributes, PropertyLocation::kDescriptor,
                    PropertyConstness::kConst, Representation::Tagged(), 0);
}

// Outputs PropertyDetails as a dictionary details.
void PropertyDetails::PrintAsSlowTo(std::ostream& os, bool print_dict_index) {
  os << "(";
  if (constness() == PropertyConstness::kConst) os << "const ";
  os << (kind() == PropertyKind::kData ? "data" : "accessor");
  if (print_dict_index) {
    os << ", dict_index: " << dictionary_index();
  }
  os << ", attrs: " << attributes() << ")";
}

// Outputs PropertyDetails as a descriptor array details.
void PropertyDetails::PrintAsFastTo(std::ostream& os, PrintMode mode) {
  os << "(";
  if (constness() == PropertyConstness::kConst) os << "const ";
  os << (kind() == PropertyKind::kData ? "data" : "accessor");
  if (location() == PropertyLocation::kField) {
    os << " field";
    if (mode & kPrintFieldIndex) {
      os << " " << field_index();
    }
    if (mode & kPrintRepresentation) {
      os << ":" << representation().Mnemonic();
    }
  } else {
    os << " descriptor";
  }
  if (mode & kPrintPointer) {
    os << ", p: " << pointer();
  }
  if (mode & kPrintAttributes) {
    os << ", attrs: " << attributes();
  }
  os << ")";
}

#ifdef OBJECT_PRINT
void PropertyDetails::Print(bool dictionary_mode) {
  StdoutStream os;
  if (dictionary_mode) {
    PrintAsSlowTo(os, true);
  } else {
    PrintAsFastTo(os, PrintMode::kPrintFull);
  }
  os << "\n" << std::flush;
}
#endif

}  // namespace internal
}  // namespace v8

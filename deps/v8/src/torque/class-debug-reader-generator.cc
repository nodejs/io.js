// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/flags/flags.h"
#include "src/torque/implementation-visitor.h"
#include "src/torque/type-oracle.h"

namespace v8::internal::torque {

constexpr char kTqObjectOverrideDecls[] =
    R"(  std::vector<std::unique_ptr<ObjectProperty>> GetProperties(
      d::MemoryAccessor accessor) const override;
  const char* GetName() const override;
  void Visit(TqObjectVisitor* visitor) const override;
  bool IsSuperclassOf(const TqObject* other) const override;
)";

namespace {
enum TypeStorage {
  kAsStoredInHeap,
  kUncompressed,
};

// An iterator for use in ValueTypeFieldsRange.
class ValueTypeFieldIterator {
 public:
  ValueTypeFieldIterator(const Type* type, size_t index)
      : type_(type), index_(index) {}
  struct Result {
    NameAndType name_and_type;
    SourcePosition pos;
    size_t offset_bytes;
    int num_bits;
    int shift_bits;
  };
  const Result operator*() const {
    if (auto struct_type = type_->StructSupertype()) {
      const auto& field = (*struct_type)->fields()[index_];
      return {field.name_and_type, field.pos, *field.offset, 0, 0};
    }
    const Type* type = type_;
    int bitfield_start_offset = 0;
    if (const auto type_wrapped_in_smi =
            Type::MatchUnaryGeneric(type_, TypeOracle::GetSmiTaggedGeneric())) {
      type = *type_wrapped_in_smi;
      bitfield_start_offset = TargetArchitecture::SmiTagAndShiftSize();
    }
    if (const BitFieldStructType* bit_field_struct_type =
            BitFieldStructType::DynamicCast(type)) {
      const auto& field = bit_field_struct_type->fields()[index_];
      return {field.name_and_type, field.pos, 0, field.num_bits,
              field.offset + bitfield_start_offset};
    }
    UNREACHABLE();
  }
  ValueTypeFieldIterator& operator++() {
    ++index_;
    return *this;
  }
  bool operator==(const ValueTypeFieldIterator& other) const {
    return type_ == other.type_ && index_ == other.index_;
  }
  bool operator!=(const ValueTypeFieldIterator& other) const {
    return !(*this == other);
  }

 private:
  const Type* type_;
  size_t index_;
};

// A way to iterate over the fields of structs or bitfield structs. For other
// types, the iterators returned from begin() and end() are immediately equal.
class ValueTypeFieldsRange {
 public:
  explicit ValueTypeFieldsRange(const Type* type) : type_(type) {}
  ValueTypeFieldIterator begin() { return {type_, 0}; }
  ValueTypeFieldIterator end() {
    size_t index = 0;
    std::optional<const StructType*> struct_type = type_->StructSupertype();
    if (struct_type && *struct_type != TypeOracle::GetFloat64OrHoleType()) {
      index = (*struct_type)->fields().size();
    }
    const Type* type = type_;
    if (const auto type_wrapped_in_smi =
            Type::MatchUnaryGeneric(type_, TypeOracle::GetSmiTaggedGeneric())) {
      type = *type_wrapped_in_smi;
    }
    if (const BitFieldStructType* bit_field_struct_type =
            BitFieldStructType::DynamicCast(type)) {
      index = bit_field_struct_type->fields().size();
    }
    return {type_, index};
  }

 private:
  const Type* type_;
};

// A convenient way to keep track of several different ways that we might need
// to represent a field's type in the generated C++.
class DebugFieldType {
 public:
  explicit DebugFieldType(const Field& field)
      : name_and_type_(field.name_and_type), pos_(field.pos) {}
  DebugFieldType(const NameAndType& name_and_type, const SourcePosition& pos)
      : name_and_type_(name_and_type), pos_(pos) {}

  bool IsTagged() const {
    return name_and_type_.type->IsSubtypeOf(TypeOracle::GetTaggedType());
  }

  // Returns the type that should be used for this field's value within code
  // that is compiled as part of the debug helper library. In particular, this
  // simplifies any tagged type to a plain uintptr_t because the debug helper
  // compiles without most of the V8 runtime code.
  std::string GetValueType(TypeStorage storage) const {
    if (IsTagged()) {
      return storage == kAsStoredInHeap ? "i::Tagged_t" : "uintptr_t";
    }

    // We can't emit a useful error at this point if the constexpr type name is
    // wrong, but we can include a comment that might be helpful.
    return GetOriginalType(storage) +
           " /*Failing? Ensure constexpr type name is correct, and the "
           "necessary #include is in any .tq file*/";
  }

  // Returns the type that should be used to represent a field's type to
  // debugging tools that have full V8 symbols. The types returned from this
  // method are resolveable in the v8::internal namespace and may refer to
  // object types that are not included in the compilation of the debug helper
  // library.
  std::string GetOriginalType(TypeStorage storage) const {
    if (name_and_type_.type->StructSupertype()) {
      // There's no meaningful type we could use here, because the V8 symbols
      // don't have any definition of a C++ struct matching this struct type.
      return "";
    }
    if (IsTagged()) {
      std::optional<const ClassType*> field_class_type =
          name_and_type_.type->ClassSupertype();
      std::string result =
          "v8::internal::" +
          (field_class_type.has_value()
               ? (*field_class_type)->GetGeneratedTNodeTypeName()
               : "Object");
      if (storage == kAsStoredInHeap) {
        result = "v8::internal::TaggedMember<" + result + ">";
      }
      return result;
    }
    return name_and_type_.type->GetConstexprGeneratedTypeName();
  }

  // Returns a C++ expression that evaluates to a string (type `const char*`)
  // containing the name of the field's type. The types returned from this
  // method are resolveable in the v8::internal namespace and may refer to
  // object types that are not included in the compilation of the debug helper
  // library.
  std::string GetTypeString(TypeStorage storage) const {
    if (IsTagged() || name_and_type_.type->IsStructType()) {
      // Wrap up the original type in a string literal.
      return "\"" + GetOriginalType(storage) + "\"";
    }

    // We require constexpr type names to be resolvable in the v8::internal
    // namespace, according to the contract in debug-helper.h. In order to
    // verify at compile time that constexpr type names are resolvable, we use
    // the type name as a dummy template parameter to a function that just
    // returns its parameter.
    return "CheckTypeName<" + GetValueType(storage) + ">(\"" +
           GetOriginalType(storage) + "\")";
  }

  // Returns the field's size in bytes.
  size_t GetSize() const {
    auto opt_size = SizeOf(name_and_type_.type);
    if (!opt_size.has_value()) {
      Error("Size required for type ", name_and_type_.type->ToString())
          .Position(pos_);
      return 0;
    }
    return std::get<0>(*opt_size);
  }

  // Returns the name of the function for getting this field's address.
  std::string GetAddressGetter() {
    return "Get" + CamelifyString(name_and_type_.name) + "Address";
  }

 private:
  NameAndType name_and_type_;
  SourcePosition pos_;
};

// Emits a function to get the address of a field within a class, based on the
// member variable {address_}, which is a tagged pointer. Example
// implementation:
//
// uintptr_t TqFixedArray::GetObjectsAddress() const {
//   return address_ - i::kHeapObjectTag + 16;
// }
void GenerateFieldAddressAccessor(const Field& field,
                                  const std::string& class_name,
                                  std::ostream& h_contents,
                                  std::ostream& cc_contents) {
  DebugFieldType debug_field_type(field);

  const std::string address_getter = debug_field_type.GetAddressGetter();

  h_contents << "  uintptr_t " << address_getter << "() const;\n";
  cc_contents << "\nuintptr_t Tq" << class_name << "::" << address_getter
              << "() const {\n";
  cc_contents << "  return address_ - i::kHeapObjectTag + " << *field.offset
              << ";\n";
  cc_contents << "}\n";
}

// Emits a function to get the value of a field, or the value from an indexed
// position within an array field, based on the member variable {address_},
// which is a tagged pointer, and the parameter {accessor}, a function pointer
// that allows for fetching memory from the debuggee. The returned result
// includes both a "validity", indicating whether the memory could be fetched,
// and the fetched value. If the field contains tagged data, then these
// functions call EnsureDecompressed to expand compressed data. Example:
//
// Value<uintptr_t> TqMap::GetPrototypeValue(d::MemoryAccessor accessor) const {
//   i::Tagged_t value{};
//   d::MemoryAccessResult validity = accessor(
//       GetPrototypeAddress(),
//       reinterpret_cast<uint8_t*>(&value),
//       sizeof(value));
//   return {validity, EnsureDecompressed(value, address_)};
// }
//
// For array fields, an offset parameter is included. Example:
//
// Value<uintptr_t> TqFixedArray::GetObjectsValue(d::MemoryAccessor accessor,
//                                                size_t offset) const {
//   i::Tagged_t value{};
//   d::MemoryAccessResult validity = accessor(
//       GetObjectsAddress() + offset * sizeof(value),
//       reinterpret_cast<uint8_t*>(&value),
//       sizeof(value));
//   return {validity, EnsureDecompressed(value, address_)};
// }
void GenerateFieldValueAccessor(const Field& field,
                                const std::string& class_name,
                                std::ostream& h_contents,
                                std::ostream& cc_contents) {
  // Currently not implemented for struct fields.
  if (field.name_and_type.type->StructSupertype()) return;

  DebugFieldType debug_field_type(field);

  const std::string address_getter = debug_field_type.GetAddressGetter();
  const std::string field_getter =
      "Get" + CamelifyString(field.name_and_type.name) + "Value";

  std::string index_param;
  std::string index_offset;
  if (field.index) {
    index_param = ", size_t offset";
    index_offset = " + offset * sizeof(value)";
  }

  std::string field_value_type = debug_field_type.GetValueType(kUncompressed);
  h_contents << "  Value<" << field_value_type << "> " << field_getter
             << "(d::MemoryAccessor accessor " << index_param << ") const;\n";
  cc_contents << "\nValue<" << field_value_type << "> Tq" << class_name
              << "::" << field_getter << "(d::MemoryAccessor accessor"
              << index_param << ") const {\n";
  cc_contents << "  " << debug_field_type.GetValueType(kAsStoredInHeap)
              << " value{};\n";
  cc_contents << "  d::MemoryAccessResult validity = accessor("
              << address_getter << "()" << index_offset
              << ", reinterpret_cast<uint8_t*>(&value), sizeof(value));\n";
#ifdef V8_MAP_PACKING
  if (field_getter == "GetMapValue") {
    cc_contents << "  value = i::MapWord::Unpack(value);\n";
  }
#endif
  cc_contents << "  return {validity, "
              << (debug_field_type.IsTagged()
                      ? "EnsureDecompressed(value, address_)"
                      : "value")
              << "};\n";
  cc_contents << "}\n";
}

// Emits a portion of the member function GetProperties that is responsible for
// adding data about the current field to a result vector called "result".
// Example output:
//
// std::vector<std::unique_ptr<StructProperty>> prototype_struct_field_list;
// result.push_back(std::make_unique<ObjectProperty>(
//     "prototype",                                     // Field name
//     "v8::internal::HeapObject",                      // Field type
//     "v8::internal::HeapObject",                      // Decompressed type
//     GetPrototypeAddress(),                           // Field address
//     1,                                               // Number of values
//     8,                                               // Size of value
//     std::move(prototype_struct_field_list),          // Struct fields
//     d::PropertyKind::kSingle));                      // Field kind
//
// In builds with pointer compression enabled, the field type for tagged values
// is "v8::internal::TaggedValue" (a four-byte class) and the decompressed type
// is a normal Object subclass that describes the expanded eight-byte type.
//
// If the field is an array, then its length is fetched from the debuggee. This
// could fail if the debuggee has incomplete memory, so the "validity" from that
// fetch is used to determine the result PropertyKind, which will say whether
// the array's length is known.
//
// If the field's type is a struct, then a local variable is created and filled
// with descriptions of each of the struct's fields. The type and decompressed
// type in the ObjectProperty are set to the empty string, to indicate to the
// caller that the struct fields vector should be used instead.
//
// The following example is an array of structs, so it uses both of the optional
// components described above:
//
// std::vector<std::unique_ptr<StructProperty>> descriptors_struct_field_list;
// descriptors_struct_field_list.push_back(std::make_unique<StructProperty>(
//     "key",                                // Struct field name
//     "v8::internal::PrimitiveHeapObject",  // Struct field type
//     "v8::internal::PrimitiveHeapObject",  // Struct field decompressed type
//     0,                                    // Byte offset within struct data
//     0,                                    // Bitfield size (0=not a bitfield)
//     0));                                  // Bitfield shift
// // The line above is repeated for other struct fields. Omitted here.
// // Fetch the slice.
// auto indexed_field_slice_descriptors =
//     TqDebugFieldSliceDescriptorArrayDescriptors(accessor, address_);
// if (indexed_field_slice_descriptors.validity == d::MemoryAccessResult::kOk) {
//   result.push_back(std::make_unique<ObjectProperty>(
//     "descriptors",                                 // Field name
//     "",                                            // Field type
//     "",                                            // Decompressed type
//     address_ - i::kHeapObjectTag +
//     std::get<1>(indexed_field_slice_descriptors.value), // Field address
//     std::get<2>(indexed_field_slice_descriptors.value), // Number of values
//     12,                                            // Size of value
//     std::move(descriptors_struct_field_list),      // Struct fields
//     GetArrayKind(indexed_field_slice_descriptors.validity)));  // Field kind
// }
void GenerateGetPropsChunkForField(const Field& field,
                                   std::ostream& get_props_impl,
                                   std::string class_name) {
  DebugFieldType debug_field_type(field);

  // If the current field is a struct or bitfield struct, create a vector
  // describing its fields. Otherwise this vector will be empty.
  std::string struct_field_list =
      field.name_and_type.name + "_struct_field_list";
  get_props_impl << "  std::vector<std::unique_ptr<StructProperty>> "
                 << struct_field_list << ";\n";
  for (const auto& struct_field :
       ValueTypeFieldsRange(field.name_and_type.type)) {
    DebugFieldType struct_field_type(struct_field.name_and_type,
                                     struct_field.pos);
    get_props_impl << "  " << struct_field_list
                   << ".push_back(std::make_unique<StructProperty>(\""
                   << struct_field.name_and_type.name << "\", "
                   << struct_field_type.GetTypeString(kAsStoredInHeap) << ", "
                   << struct_field.offset_bytes << ", " << struct_field.num_bits
                   << ", " << struct_field.shift_bits << "));\n";
  }
  struct_field_list = "std::move(" + struct_field_list + ")";

  // The number of values and property kind for non-indexed properties:
  std::string count_value = "1";
  std::string property_kind = "d::PropertyKind::kSingle";

  // If the field is indexed, emit a fetch of the array length, and change
  // count_value and property_kind to be the correct values for an array.
  if (field.index) {
    std::string indexed_field_slice =
        "indexed_field_slice_" + field.name_and_type.name;
    get_props_impl << "  auto " << indexed_field_slice << " = "
                   << "TqDebugFieldSlice" << class_name
                   << CamelifyString(field.name_and_type.name)
                   << "(accessor, address_);\n";
    std::string validity = indexed_field_slice + ".validity";
    std::string value = indexed_field_slice + ".value";
    property_kind = "GetArrayKind(" + validity + ")";

    get_props_impl << "  if (" << validity
                   << " == d::MemoryAccessResult::kOk) {\n"
                   << "    result.push_back(std::make_unique<ObjectProperty>(\""
                   << field.name_and_type.name << "\", "
                   << debug_field_type.GetTypeString(kAsStoredInHeap) << ", "
                   << "address_ - i::kHeapObjectTag + std::get<1>(" << value
                   << "), "
                   << "std::get<2>(" << value << ")"
                   << ", " << debug_field_type.GetSize() << ", "
                   << struct_field_list << ", " << property_kind << "));\n"
                   << "  }\n";
    return;
  }
  get_props_impl << "  result.push_back(std::make_unique<ObjectProperty>(\""
                 << field.name_and_type.name << "\", "
                 << debug_field_type.GetTypeString(kAsStoredInHeap) << ", "
                 << debug_field_type.GetAddressGetter() << "(), " << count_value
                 << ", " << debug_field_type.GetSize() << ", "
                 << struct_field_list << ", " << property_kind << "));\n";
}

// For any Torque-defined class Foo, this function generates a class TqFoo which
// allows for convenient inspection of objects of type Foo in a crash dump or
// time travel session (where we can't just run the object printer). The
// generated class looks something like this:
//
// class TqFoo : public TqParentOfFoo {
//  public:
//   // {address} is an uncompressed tagged pointer.
//   inline TqFoo(uintptr_t address) : TqParentOfFoo(address) {}
//
//   // Creates and returns a list of this object's properties.
//   std::vector<std::unique_ptr<ObjectProperty>> GetProperties(
//       d::MemoryAccessor accessor) const override;
//
//   // Returns the name of this class, "v8::internal::Foo".
//   const char* GetName() const override;
//
//   // Visitor pattern; implementation just calls visitor->VisitFoo(this).
//   void Visit(TqObjectVisitor* visitor) const override;
//
//   // Returns whether Foo is a superclass of the other object's type.
//   bool IsSuperclassOf(const TqObject* other) const override;
//
//   // Field accessors omitted here (see other comments above).
// };
//
// Four output streams are written:
//
// h_contents:  A header file which gets the class definition above.
// cc_contents: A cc file which gets implementations of that class's members.
// visitor:     A stream that is accumulating the definition of the class
//              TqObjectVisitor. Each class Foo gets its own virtual method
//              VisitFoo in TqObjectVisitor.
void GenerateClassDebugReader(const ClassType& type, std::ostream& h_contents,
                              std::ostream& cc_contents, std::ostream& visitor,
                              std::unordered_set<const ClassType*>* done) {
  // Make sure each class only gets generated once.
  if (!done->insert(&type).second) return;
  const ClassType* super_type = type.GetSuperClass();

  // We must emit the classes in dependency order. If the super class hasn't
  // been emitted yet, go handle it first.
  if (super_type != nullptr) {
    GenerateClassDebugReader(*super_type, h_contents, cc_contents, visitor,
                             done);
  }

  // Classes with undefined layout don't grant any particular value here and may
  // not correspond with actual C++ classes, so skip them.
  if (type.HasUndefinedLayout()) return;

  const std::string name = type.name();
  const std::string super_name =
      super_type == nullptr ? "Object" : super_type->name();
  h_contents << "\nclass Tq" << name << " : public Tq" << super_name << " {\n";
  h_contents << " public:\n";
  h_contents << "  inline Tq" << name << "(uintptr_t address) : Tq"
             << super_name << "(address) {}\n";
  h_contents << kTqObjectOverrideDecls;

  cc_contents << "\nconst char* Tq" << name << "::GetName() const {\n";
  cc_contents << "  return \"v8::internal::" << name << "\";\n";
  cc_contents << "}\n";

  cc_contents << "\nvoid Tq" << name
              << "::Visit(TqObjectVisitor* visitor) const {\n";
  cc_contents << "  visitor->Visit" << name << "(this);\n";
  cc_contents << "}\n";

  cc_contents << "\nbool Tq" << name
              << "::IsSuperclassOf(const TqObject* other) const {\n";
  cc_contents
      << "  return GetName() != other->GetName() && dynamic_cast<const Tq"
      << name << "*>(other) != nullptr;\n";
  cc_contents << "}\n";

  // By default, the visitor method for this class just calls the visitor method
  // for this class's parent. This allows custom visitors to only override a few
  // classes they care about without needing to know about the entire hierarchy.
  visitor << "  virtual void Visit" << name << "(const Tq" << name
          << "* object) {\n";
  visitor << "    Visit" << super_name << "(object);\n";
  visitor << "  }\n";

  std::stringstream get_props_impl;

  for (const Field& field : type.fields()) {
    if (field.name_and_type.type == TypeOracle::GetVoidType()) continue;
    if (field.offset.has_value()) {
      GenerateFieldAddressAccessor(field, name, h_contents, cc_contents);
      GenerateFieldValueAccessor(field, name, h_contents, cc_contents);
    }
    GenerateGetPropsChunkForField(field, get_props_impl, name);
  }

  h_contents << "};\n";

  cc_contents << "\nstd::vector<std::unique_ptr<ObjectProperty>> Tq" << name
              << "::GetProperties(d::MemoryAccessor accessor) const {\n";
  // Start by getting the fields from the parent class.
  cc_contents << "  std::vector<std::unique_ptr<ObjectProperty>> result = Tq"
              << super_name << "::GetProperties(accessor);\n";
  // Then add the fields from this class.
  cc_contents << get_props_impl.str();
  cc_contents << "  return result;\n";
  cc_contents << "}\n";
}
}  // namespace

void ImplementationVisitor::GenerateClassDebugReaders(
    const std::string& output_directory) {
  const std::string file_name = "class-debug-readers";
  std::stringstream h_contents;
  std::stringstream cc_contents;
  h_contents << "// Provides the ability to read object properties in\n";
  h_contents << "// postmortem or remote scenarios, where the debuggee's\n";
  h_contents << "// memory is not part of the current process's address\n";
  h_contents << "// space and must be read using a callback function.\n\n";
  {
    IncludeGuardScope include_guard(h_contents, file_name + ".h");

    h_contents << "#include <cstdint>\n";
    h_contents << "#include <vector>\n\n";

    for (const std::string& include_path : GlobalContext::CppIncludes()) {
      h_contents << "#include " << StringLiteralQuote(include_path) << "\n";
    }

    h_contents
        << "\n#include \"tools/debug_helper/debug-helper-internal.h\"\n\n";

    const char* kWingdiWorkaround =
        "// Unset a wingdi.h macro that causes conflicts.\n"
        "#ifdef GetBValue\n"
        "#undef GetBValue\n"
        "#endif\n\n";

    h_contents << kWingdiWorkaround;

    cc_contents << "#include \"torque-generated/" << file_name << ".h\"\n\n";
    cc_contents << "#include \"src/objects/all-objects-inl.h\"\n";
    cc_contents << "#include \"torque-generated/debug-macros.h\"\n\n";
    cc_contents << kWingdiWorkaround;
    cc_contents << "namespace i = v8::internal;\n\n";

    NamespaceScope h_namespaces(h_contents,
                                {"v8", "internal", "debug_helper_internal"});
    NamespaceScope cc_namespaces(cc_contents,
                                 {"v8", "internal", "debug_helper_internal"});

    std::stringstream visitor;
    visitor << "\nclass TqObjectVisitor {\n";
    visitor << " public:\n";
    visitor << "  virtual void VisitObject(const TqObject* object) {}\n";

    std::unordered_set<const ClassType*> done;
    for (const ClassType* type : TypeOracle::GetClasses()) {
      GenerateClassDebugReader(*type, h_contents, cc_contents, visitor, &done);
    }

    visitor << "};\n";
    h_contents << visitor.str();
  }
  WriteFile(output_directory + "/" + file_name + ".h", h_contents.str());
  WriteFile(output_directory + "/" + file_name + ".cc", cc_contents.str());
}

}  // namespace v8::internal::torque

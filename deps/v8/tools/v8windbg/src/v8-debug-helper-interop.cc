// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/v8windbg/src/v8-debug-helper-interop.h"

#include <Windows.h>
#include <crtdbg.h>

#include "src/common/globals.h"
#include "tools/debug_helper/debug-helper.h"
#include "tools/v8windbg/base/utilities.h"
#include "tools/v8windbg/src/v8windbg-extension.h"

namespace d = v8::debug_helper;

// We need a plain C function pointer for interop with v8_debug_helper. We can
// use this to get one as long as we never need two at once.
class V8_NODISCARD MemReaderScope {
 public:
  explicit MemReaderScope(WRL::ComPtr<IDebugHostContext> sp_context)
      : sp_context_(sp_context) {
    _ASSERTE(!context_);
    context_ = sp_context_.Get();
  }
  ~MemReaderScope() { context_ = nullptr; }
  d::MemoryAccessor GetReader() { return &MemReaderScope::Read; }

 private:
  MemReaderScope(const MemReaderScope&) = delete;
  MemReaderScope& operator=(const MemReaderScope&) = delete;
  static d::MemoryAccessResult Read(uintptr_t address, void* destination,
                                    size_t byte_count) {
    ULONG64 bytes_read;
    Location loc{address};
    HRESULT hr = sp_debug_host_memory->ReadBytes(context_, loc, destination,
                                                 byte_count, &bytes_read);
    // TODO determine when an address is valid but inaccessible
    return SUCCEEDED(hr) ? d::MemoryAccessResult::kOk
                         : d::MemoryAccessResult::kAddressNotValid;
  }
  WRL::ComPtr<IDebugHostContext> sp_context_;
  static IDebugHostContext* context_;
};
IDebugHostContext* MemReaderScope::context_;

StructField::StructField(std::u16string field_name, std::u16string type_name,
                         uint64_t offset, uint8_t num_bits, uint8_t shift_bits)
    : name(field_name),
      type_name(type_name),
      offset(offset),
      num_bits(num_bits),
      shift_bits(shift_bits) {}
StructField::~StructField() = default;
StructField::StructField(const StructField&) = default;
StructField::StructField(StructField&&) = default;
StructField& StructField::operator=(const StructField&) = default;
StructField& StructField::operator=(StructField&&) = default;

Property::Property(std::u16string property_name, std::u16string type_name,
                   uint64_t address, size_t item_size)
    : name(property_name),
      type(PropertyType::kPointer),
      type_name(type_name),
      addr_value(address),
      item_size(item_size) {}
Property::~Property() = default;
Property::Property(const Property&) = default;
Property::Property(Property&&) = default;
Property& Property::operator=(const Property&) = default;
Property& Property::operator=(Property&&) = default;

V8HeapObject::V8HeapObject() = default;
V8HeapObject::~V8HeapObject() = default;
V8HeapObject::V8HeapObject(const V8HeapObject&) = default;
V8HeapObject::V8HeapObject(V8HeapObject&&) = default;
V8HeapObject& V8HeapObject::operator=(const V8HeapObject&) = default;
V8HeapObject& V8HeapObject::operator=(V8HeapObject&&) = default;

std::vector<Property> GetPropertiesAsVector(size_t num_properties,
                                            d::ObjectProperty** properties) {
  std::vector<Property> result;
  for (size_t property_index = 0; property_index < num_properties;
       ++property_index) {
    const auto& source_prop = *(properties)[property_index];
    Property dest_prop(ConvertToU16String(source_prop.name),
                       ConvertToU16String(source_prop.type),
                       source_prop.address, source_prop.size);
    if (source_prop.kind != d::PropertyKind::kSingle) {
      dest_prop.type = PropertyType::kArray;
      dest_prop.length = source_prop.num_values;
    }
    if (dest_prop.type_name.empty() || source_prop.num_struct_fields > 0) {
      // If the helper library didn't provide a type, then it should have
      // provided struct fields instead. Set the struct type flag and copy the
      // fields into the result.
      dest_prop.type =
          static_cast<PropertyType>(static_cast<int>(dest_prop.type) |
                                    static_cast<int>(PropertyType::kStruct));
      for (size_t field_index = 0; field_index < source_prop.num_struct_fields;
           ++field_index) {
        const auto& struct_field = *source_prop.struct_fields[field_index];
        dest_prop.fields.push_back({ConvertToU16String(struct_field.name),
                                    ConvertToU16String(struct_field.type),
                                    struct_field.offset, struct_field.num_bits,
                                    struct_field.shift_bits});
      }
    }
    result.push_back(dest_prop);
  }
  return result;
}

HRESULT GetMetadataPointerTableAddress(WRL::ComPtr<IDebugHostContext> context,
                                       uintptr_t* result) {
  WRL::ComPtr<IDebugHostType> memory_chunk_type =
      Extension::Current()->GetTypeFromV8Module(context,
                                                u"v8::internal::MemoryChunk");
  if (memory_chunk_type == nullptr) return E_FAIL;
  WRL::ComPtr<IModelObject> memory_chunk_instance;
  // This is sort of awkward, but the most ergonomic way to get a static field
  // is by creating a typed object at a made-up address and then getting its
  // field. Essentially this is doing:
  //   ((MemoryChunk*)0)->metadata_pointer_table_
  RETURN_IF_FAIL(sp_data_model_manager->CreateTypedObject(
      context.Get(), Location{0}, memory_chunk_type.Get(),
      &memory_chunk_instance));
  WRL::ComPtr<IModelObject> metadata_pointer_table;
  RETURN_IF_FAIL(memory_chunk_instance->GetRawValue(
      SymbolKind::SymbolField, L"metadata_pointer_table_", RawSearchNone,
      &metadata_pointer_table));
  Location location;
  RETURN_IF_FAIL(metadata_pointer_table->GetLocation(&location));
  *result = location.Offset;
  return S_OK;
}

V8HeapObject GetHeapObject(WRL::ComPtr<IDebugHostContext> sp_context,
                           uint64_t tagged_ptr, uint64_t referring_pointer,
                           const char* type_name, bool is_compressed) {
  // Read the value at the address, and see if it is a tagged pointer

  V8HeapObject obj;
  MemReaderScope reader_scope(sp_context);

  d::HeapAddresses heap_addresses = {0, 0, 0, 0, 0};
  // TODO ideally we'd provide real heap page pointers. For now, just testing
  // decompression based on the pointer to wherever we found this value,
  // which is likely (though not guaranteed) to be a heap pointer itself.
  heap_addresses.any_heap_pointer = referring_pointer;

  // Ignore the return value; there is nothing useful we can do in case of
  // failure.
  GetMetadataPointerTableAddress(sp_context,
                                 &heap_addresses.metadata_pointer_table);

  auto props = d::GetObjectProperties(tagged_ptr, reader_scope.GetReader(),
                                      heap_addresses, type_name);
  obj.friendly_name = ConvertToU16String(props->brief);
  obj.properties =
      GetPropertiesAsVector(props->num_properties, props->properties);

  // For each guessed type, create a synthetic property that will request data
  // about the same object again but with a more specific type hint.
  if (referring_pointer != 0) {
    for (size_t type_index = 0; type_index < props->num_guessed_types;
         ++type_index) {
      const std::string& guessed_type_name = props->guessed_types[type_index];
      Property dest_prop(
          ConvertToU16String(("guessed type " + guessed_type_name).c_str()),
          ConvertToU16String(guessed_type_name), referring_pointer,
          is_compressed ? i::kTaggedSize : sizeof(void*));
      obj.properties.push_back(dest_prop);
    }
  }

  return obj;
}

const char* BitsetName(uint64_t payload) { return d::BitsetName(payload); }

std::vector<Property> GetStackFrame(WRL::ComPtr<IDebugHostContext> sp_context,

                                    uint64_t frame_pointer) {
  MemReaderScope reader_scope(sp_context);
  auto props = d::GetStackFrame(static_cast<uintptr_t>(frame_pointer),
                                reader_scope.GetReader());
  return GetPropertiesAsVector(props->num_properties, props->properties);
}

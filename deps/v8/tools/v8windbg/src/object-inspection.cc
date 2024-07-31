// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/v8windbg/src/object-inspection.h"

#include "src/flags/flags.h"
#include "tools/v8windbg/base/utilities.h"
#include "tools/v8windbg/src/v8-debug-helper-interop.h"
#include "tools/v8windbg/src/v8windbg-extension.h"

V8CachedObject::V8CachedObject(Location location,
                               std::string uncompressed_type_name,
                               WRL::ComPtr<IDebugHostContext> context,
                               bool is_compressed)
    : location_(std::move(location)),
      uncompressed_type_name_(std::move(uncompressed_type_name)),
      context_(std::move(context)),
      is_compressed_(is_compressed) {}

HRESULT V8CachedObject::Create(IModelObject* p_v8_object_instance,
                               IV8CachedObject** result) {
  Location location;
  RETURN_IF_FAIL(p_v8_object_instance->GetLocation(&location));

  WRL::ComPtr<IDebugHostContext> context;
  RETURN_IF_FAIL(p_v8_object_instance->GetContext(&context));

  WRL::ComPtr<IDebugHostType> sp_tagged_type;
  RETURN_IF_FAIL(p_v8_object_instance->GetTypeInfo(&sp_tagged_type));

  // The type is some Tagged<T>, so we need to get the first generic type
  // parameter.
  bool is_generic;
  RETURN_IF_FAIL(sp_tagged_type->IsGeneric(&is_generic));
  if (!is_generic) return E_FAIL;

  WRL::ComPtr<IDebugHostSymbol> sp_generic_arg;
  RETURN_IF_FAIL(sp_tagged_type->GetGenericArgumentAt(0, &sp_generic_arg));

  _bstr_t type_name;
  RETURN_IF_FAIL(sp_generic_arg->GetName(type_name.GetAddress()));

  bool is_compressed = false;
  *result =
      WRL::Make<V8CachedObject>(location, static_cast<const char*>(type_name),
                                context, is_compressed)
          .Detach();
  return S_OK;
}

V8CachedObject::V8CachedObject(V8HeapObject heap_object)
    : heap_object_(std::move(heap_object)), heap_object_initialized_(true) {}

V8CachedObject::~V8CachedObject() = default;

IFACEMETHODIMP V8CachedObject::GetCachedV8HeapObject(
    V8HeapObject** pp_heap_object) noexcept {
  if (!heap_object_initialized_) {
    heap_object_initialized_ = true;
    uint64_t tagged_ptr = 0;
    uint64_t bytes_read;
    HRESULT hr = sp_debug_host_memory->ReadBytes(
        context_.Get(), location_, reinterpret_cast<void*>(&tagged_ptr),
        is_compressed_ ? i::kTaggedSize : sizeof(void*), &bytes_read);
    // S_FALSE can be returned if fewer bytes were read than were requested. We
    // need all of the bytes, so check for S_OK.
    if (hr != S_OK) {
      std::stringstream message;
      message << "Unable to read memory";
      if (location_.IsVirtualAddress()) {
        message << " at 0x" << std::hex << location_.GetOffset();
      }
      heap_object_.friendly_name = ConvertToU16String(message.str());
    } else {
      if (is_compressed_)
        tagged_ptr = ExpandCompressedPointer(static_cast<uint32_t>(tagged_ptr));
      heap_object_ =
          ::GetHeapObject(context_, tagged_ptr, location_.GetOffset(),
                          uncompressed_type_name_.c_str(), is_compressed_);
    }
  }
  *pp_heap_object = &this->heap_object_;
  return S_OK;
}

bool TryUnwrapTaggedMemberType(const std::u16string& type,
                               std::u16string* result) {
  std::u16string prefix = u"v8::internal::TaggedMember<";
  if (type.substr(0, prefix.length()) == prefix && type.back() == u'>') {
    if (result) {
      *result =
          type.substr(prefix.length(), type.length() - prefix.length() - 1);
    }
    return true;
  }
  return false;
}

IndexedFieldData::IndexedFieldData(Property property)
    : property_(std::move(property)) {}

IndexedFieldData::~IndexedFieldData() = default;

IFACEMETHODIMP IndexedFieldData::GetProperty(Property** property) noexcept {
  if (!property) return E_POINTER;
  *property = &this->property_;
  return S_OK;
}

V8ObjectKeyEnumerator::V8ObjectKeyEnumerator(
    WRL::ComPtr<IV8CachedObject>& v8_cached_object)
    : sp_v8_cached_object_{v8_cached_object} {}
V8ObjectKeyEnumerator::~V8ObjectKeyEnumerator() = default;

IFACEMETHODIMP V8ObjectKeyEnumerator::Reset() noexcept {
  index_ = 0;
  return S_OK;
}

IFACEMETHODIMP V8ObjectKeyEnumerator::GetNext(BSTR* key, IModelObject** value,
                                              IKeyStore** metadata) noexcept {
  V8HeapObject* p_v8_heap_object;
  sp_v8_cached_object_->GetCachedV8HeapObject(&p_v8_heap_object);

  if (static_cast<size_t>(index_) >= p_v8_heap_object->properties.size())
    return E_BOUNDS;

  auto* name_ptr = p_v8_heap_object->properties[index_].name.c_str();
  *key = ::SysAllocString(U16ToWChar(name_ptr));
  ++index_;
  return S_OK;
}

IFACEMETHODIMP V8LocalDataModel::InitializeObject(
    IModelObject* model_object,
    IDebugHostTypeSignature* matching_type_signature,
    IDebugHostSymbolEnumerator* wildcard_matches) noexcept {
  return S_OK;
}

IFACEMETHODIMP V8LocalDataModel::GetName(BSTR* model_name) noexcept {
  return E_NOTIMPL;
}

IFACEMETHODIMP V8ObjectDataModel::InitializeObject(
    IModelObject* model_object,
    IDebugHostTypeSignature* matching_type_signature,
    IDebugHostSymbolEnumerator* wildcard_matches) noexcept {
  return S_OK;
}

IFACEMETHODIMP V8ObjectDataModel::GetName(BSTR* model_name) noexcept {
  return E_NOTIMPL;
}

IFACEMETHODIMP V8ObjectDataModel::ToDisplayString(
    IModelObject* context_object, IKeyStore* metadata,
    BSTR* display_string) noexcept {
  WRL::ComPtr<IV8CachedObject> sp_v8_cached_object;
  RETURN_IF_FAIL(GetCachedObject(context_object, &sp_v8_cached_object));
  V8HeapObject* p_v8_heap_object;
  RETURN_IF_FAIL(sp_v8_cached_object->GetCachedV8HeapObject(&p_v8_heap_object));
  *display_string = ::SysAllocString(
      reinterpret_cast<const wchar_t*>(p_v8_heap_object->friendly_name.data()));
  return S_OK;
}

namespace {

// Creates a synthetic object, attaches a parent model, and sets the context
// object for that parent data model. Caller is responsible for ensuring that
// the parent model's Concepts have been initialized correctly and that the
// data model context is of an appropriate type for the parent model.
HRESULT CreateSyntheticObjectWithParentAndDataContext(
    IDebugHostContext* ctx, IModelObject* parent_model, IUnknown* data_context,
    IModelObject** result) {
  WRL::ComPtr<IModelObject> value;
  RETURN_IF_FAIL(sp_data_model_manager->CreateSyntheticObject(ctx, &value));
  RETURN_IF_FAIL(
      value->AddParentModel(parent_model, nullptr, true /*override*/));
  RETURN_IF_FAIL(value->SetContextForDataModel(parent_model, data_context));
  *result = value.Detach();
  return S_OK;
}

// Creates an IModelObject for a V8 object whose value is represented by the
// data in cached_object. This is an alternative to  CreateTypedObject for
// particularly complex cases (compressed values and those that don't exist
// anywhere in memory).
HRESULT CreateSyntheticObjectForV8Object(IDebugHostContext* ctx,
                                         V8CachedObject* cached_object,
                                         IModelObject** result) {
  // Explicitly add the parent model and data context. On a plain typed object,
  // the parent model would be attached automatically because we registered for
  // a matching type signature, and the data context would be set during
  // V8ObjectDataModel::GetCachedObject.
  return CreateSyntheticObjectWithParentAndDataContext(
      ctx, Extension::Current()->GetObjectDataModel(), cached_object, result);
}

// Creates an IModelObject to represent a field that is not a struct or array.
HRESULT GetModelForBasicField(const uint64_t address,
                              const std::u16string& type_name,
                              WRL::ComPtr<IDebugHostContext>& sp_ctx,
                              IModelObject** result) {
  // We can't currently look up TaggedMember types for two reasons:
  // 1. We need to insert the default template parameter; TaggedMember<T> is
  //    actually TaggedMember<T, V8HeapCompressionScheme>.
  // 2. No TaggedMember classes are currently instantiated in the V8 module,
  //    and the debugger won't invent new template types.
  // Thus, we must check using string manipulation whether this field is a
  // TaggedMember, and create a synthetic object for the field.
  std::u16string unwrapped_type_name;
  if (!TryUnwrapTaggedMemberType(type_name, &unwrapped_type_name)) {
    WRL::ComPtr<IDebugHostType> type =
        Extension::Current()->GetTypeFromV8Module(sp_ctx, type_name.c_str());
    if (type == nullptr) return E_FAIL;
    return sp_data_model_manager->CreateTypedObject(
        sp_ctx.Get(), Location{address}, type.Get(), result);
  }

  // For tagged fields, we need to do something a little more
  // complicated. We could just use CreateTypedObject with the type
  // v8::internal::TaggedValue, but then we'd sacrifice any other data
  // that we've learned about the field's specific type. So instead we
  // create a synthetic object.
  WRL::ComPtr<V8CachedObject> cached_object = WRL::Make<V8CachedObject>(
      Location(address), ConvertFromU16String(unwrapped_type_name), sp_ctx,
      COMPRESS_POINTERS_BOOL);
  return CreateSyntheticObjectForV8Object(sp_ctx.Get(), cached_object.Get(),
                                          result);
}

// Creates an IModelObject representing the value of a bitfield.
HRESULT GetModelForBitField(uint64_t address, const uint8_t num_bits,
                            uint8_t shift_bits, const std::u16string& type_name,
                            WRL::ComPtr<IDebugHostContext>& sp_ctx,
                            IModelObject** result) {
  // Look up the type by name.
  WRL::ComPtr<IDebugHostType> type =
      Extension::Current()->GetTypeFromV8Module(sp_ctx, type_name.c_str());
  if (type == nullptr) return E_FAIL;

  // Figure out exactly which bytes contain the bitfield's data. This depends on
  // platform byte order (little-endian for Windows).
  constexpr int kBitsPerByte = 8;
  uint8_t shift_bytes = shift_bits / kBitsPerByte;
  address += shift_bytes;
  shift_bits -= shift_bytes * kBitsPerByte;
  size_t bits_to_read = shift_bits + num_bits;
  size_t bytes_to_read = (bits_to_read + kBitsPerByte - 1) / kBitsPerByte;

  uintptr_t value = 0;

  // V8 guarantees that bitfield structs are no bigger than a single pointer.
  if (bytes_to_read > sizeof(value)) {
    std::stringstream message;
    message << "Fatal v8windbg error: found bitfield struct of "
            << bytes_to_read << "bytes, which exceeds the supported size of "
            << sizeof(value);
    return CreateString(ConvertToU16String(message.str()), result);
  }

  uint64_t bytes_read;
  HRESULT hr = sp_debug_host_memory->ReadBytes(sp_ctx.Get(), address,
                                               reinterpret_cast<void*>(&value),
                                               bytes_to_read, &bytes_read);

  // S_FALSE can be returned if fewer bytes were read than were requested. We
  // need all of the bytes, so check for S_OK.
  if (hr != S_OK) {
    std::stringstream message;
    message << "Unable to read memory at 0x" << std::hex << address;
    return CreateString(ConvertToU16String(message.str()), result);
  }

  // Decode the bitfield.
  value = (value >> shift_bits) & ((1 << num_bits) - 1);

  return CreateTypedIntrinsic(value, type.Get(), result);
}

// Creates an IModelObject to represent the packed fields in a Torque struct.
// Note that Torque structs are not C++ structs and do not have any type
// definitions in the V8 symbols.
HRESULT GetModelForStruct(const uint64_t address,
                          const std::vector<StructField>& fields,
                          WRL::ComPtr<IDebugHostContext>& sp_ctx,
                          IModelObject** result) {
  WRL::ComPtr<IModelObject> sp_value;
  RETURN_IF_FAIL(
      sp_data_model_manager->CreateSyntheticObject(sp_ctx.Get(), &sp_value));

  // There's no need for any fancy Concepts here; just add key-value pairs for
  // each field.
  for (const StructField& field : fields) {
    WRL::ComPtr<IModelObject> field_model;
    if (field.num_bits == 0) {
      if (FAILED(GetModelForBasicField(address + field.offset, field.type_name,
                                       sp_ctx, &field_model))) {
        continue;
      }
    } else {
      if (FAILED(GetModelForBitField(address + field.offset, field.num_bits,
                                     field.shift_bits, field.type_name, sp_ctx,
                                     &field_model))) {
        continue;
      }
    }
    RETURN_IF_FAIL(
        sp_value->SetKey(reinterpret_cast<const wchar_t*>(field.name.c_str()),
                         field_model.Get(), nullptr));
  }

  *result = sp_value.Detach();
  return S_OK;
}

// Creates an IModelObject representing an array of some type that we expect to
// be defined in the V8 symbols.
HRESULT GetModelForNativeArray(const uint64_t address,
                               const std::u16string& type_name, size_t count,
                               WRL::ComPtr<IDebugHostContext>& sp_ctx,
                               IModelObject** result) {
  WRL::ComPtr<IDebugHostType> type =
      Extension::Current()->GetTypeFromV8Module(sp_ctx, type_name.c_str());
  if (type == nullptr) return E_FAIL;

  ULONG64 object_size{};
  RETURN_IF_FAIL(type->GetSize(&object_size));

  ArrayDimension dimensions[] = {
      {/*start=*/0, /*length=*/count, /*stride=*/object_size}};
  WRL::ComPtr<IDebugHostType> array_type;
  RETURN_IF_FAIL(
      type->CreateArrayOf(/*dimensions=*/1, dimensions, &array_type));

  return sp_data_model_manager->CreateTypedObject(
      sp_ctx.Get(), Location{address}, array_type.Get(), result);
}

// Creates an IModelObject that represents an array of structs or compressed
// tagged values.
HRESULT GetModelForCustomArray(const Property& prop,
                               WRL::ComPtr<IDebugHostContext>& sp_ctx,
                               IModelObject** result) {
  // Create the context which should be provided to the indexing and iterating
  // functionality provided by the parent model. This is instance-specific data,
  // whereas the parent model object could be shared among many custom arrays.
  WRL::ComPtr<IndexedFieldData> context_data =
      WRL::Make<IndexedFieldData>(prop);

  return CreateSyntheticObjectWithParentAndDataContext(
      sp_ctx.Get(), Extension::Current()->GetIndexedFieldDataModel(),
      context_data.Get(), result);
}


// Creates an IModelObject representing the data in an array at the given index.
// context_object is expected to be an object of the form created by
// GetModelForCustomArray, meaning its context for the IndexedFieldParent data
// model is an IIndexedFieldData containing the description of the array.
HRESULT GetModelForCustomArrayElement(IModelObject* context_object,
                                      size_t index, IModelObject** object) {
  // Open a few layers of wrapper objects to get to the Property object that
  // describes the array.
  WRL::ComPtr<IUnknown> data_model_context;
  RETURN_IF_FAIL(context_object->GetContextForDataModel(
      Extension::Current()->GetIndexedFieldDataModel(), &data_model_context));
  WRL::ComPtr<IIndexedFieldData> indexed_field_data;
  RETURN_IF_FAIL(data_model_context.As(&indexed_field_data));
  Property* prop;
  RETURN_IF_FAIL(indexed_field_data->GetProperty(&prop));

  if (index >= prop->length) {
    return E_BOUNDS;
  }

  WRL::ComPtr<IDebugHostContext> sp_ctx;
  RETURN_IF_FAIL(context_object->GetContext(&sp_ctx));

  ULONG64 address = prop->addr_value + index * prop->item_size;

  switch (prop->type) {
    case PropertyType::kArray:
      return GetModelForBasicField(address, prop->type_name, sp_ctx, object);
    case PropertyType::kStructArray:
      return GetModelForStruct(address, prop->fields, sp_ctx, object);
    default:
      return E_FAIL;  // Only array properties should be possible here.
  }
}

}  // namespace

IFACEMETHODIMP IndexedFieldParent::InitializeObject(
    IModelObject* model_object,
    IDebugHostTypeSignature* matching_type_signature,
    IDebugHostSymbolEnumerator* wildcard_matches) noexcept {
  return S_OK;
}

IFACEMETHODIMP IndexedFieldParent::GetName(BSTR* model_name) noexcept {
  return E_NOTIMPL;
}

IFACEMETHODIMP IndexedFieldParent::GetDimensionality(
    IModelObject* context_object, ULONG64* dimensionality) noexcept {
  *dimensionality = 1;
  return S_OK;
}

IFACEMETHODIMP IndexedFieldParent::GetAt(IModelObject* context_object,
                                         ULONG64 indexer_count,
                                         IModelObject** indexers,
                                         IModelObject** object,
                                         IKeyStore** metadata) noexcept {
  if (indexer_count != 1) return E_INVALIDARG;
  if (metadata != nullptr) *metadata = nullptr;

  ULONG64 index;
  RETURN_IF_FAIL(UnboxULong64(indexers[0], &index, /*convert=*/true));

  return GetModelForCustomArrayElement(context_object, index, object);
}

IFACEMETHODIMP IndexedFieldParent::SetAt(IModelObject* context_object,
                                         ULONG64 indexer_count,
                                         IModelObject** indexers,
                                         IModelObject* value) noexcept {
  return E_NOTIMPL;
}

IFACEMETHODIMP IndexedFieldParent::GetDefaultIndexDimensionality(
    IModelObject* context_object, ULONG64* dimensionality) noexcept {
  *dimensionality = 1;
  return S_OK;
}

IFACEMETHODIMP IndexedFieldParent::GetIterator(
    IModelObject* context_object, IModelIterator** iterator) noexcept {
  auto indexed_field_iterator{WRL::Make<IndexedFieldIterator>(context_object)};
  *iterator = indexed_field_iterator.Detach();
  return S_OK;
}

IndexedFieldIterator::IndexedFieldIterator(IModelObject* context_object)
    : context_object_(context_object) {}
IndexedFieldIterator::~IndexedFieldIterator() = default;

IFACEMETHODIMP IndexedFieldIterator::Reset() noexcept {
  next_ = 0;
  return S_OK;
}

IFACEMETHODIMP IndexedFieldIterator::GetNext(IModelObject** object,
                                             ULONG64 dimensions,
                                             IModelObject** indexers,
                                             IKeyStore** metadata) noexcept {
  if (dimensions > 1) return E_INVALIDARG;

  WRL::ComPtr<IModelObject> sp_index, sp_value;
  RETURN_IF_FAIL(
      GetModelForCustomArrayElement(context_object_.Get(), next_, &sp_value));
  RETURN_IF_FAIL(CreateULong64(next_, &sp_index));

  // Everything that could fail (including the bounds check) has succeeded, so
  // increment the index.
  ++next_;

  // Write results (none of these steps can fail, which is important because we
  // transfer ownership of two separate objects).
  if (dimensions == 1) {
    indexers[0] = sp_index.Detach();
  }
  *object = sp_value.Detach();
  if (metadata != nullptr) *metadata = nullptr;
  return S_OK;
}

IFACEMETHODIMP V8ObjectDataModel::GetKey(IModelObject* context_object,
                                         PCWSTR key, IModelObject** key_value,
                                         IKeyStore** metadata,
                                         bool* has_key) noexcept {
  if (metadata != nullptr) *metadata = nullptr;

  WRL::ComPtr<IV8CachedObject> sp_v8_cached_object;
  RETURN_IF_FAIL(GetCachedObject(context_object, &sp_v8_cached_object));
  V8HeapObject* p_v8_heap_object;
  RETURN_IF_FAIL(sp_v8_cached_object->GetCachedV8HeapObject(&p_v8_heap_object));

  *has_key = false;
  for (const auto& prop : p_v8_heap_object->properties) {
    const char16_t* p_key = reinterpret_cast<const char16_t*>(key);
    if (prop.name.compare(p_key) == 0) {
      *has_key = true;
      if (key_value != nullptr) {
        WRL::ComPtr<IDebugHostContext> sp_ctx;
        RETURN_IF_FAIL(context_object->GetContext(&sp_ctx));
        RETURN_IF_FAIL(GetModelForProperty(prop, sp_ctx, key_value));
      }
      return S_OK;
    }
  }

  return S_OK;
}

IFACEMETHODIMP V8ObjectDataModel::SetKey(IModelObject* context_object,
                                         PCWSTR key, IModelObject* key_value,
                                         IKeyStore* metadata) noexcept {
  return E_NOTIMPL;
}

IFACEMETHODIMP V8ObjectDataModel::EnumerateKeys(
    IModelObject* context_object, IKeyEnumerator** pp_enumerator) noexcept {
  WRL::ComPtr<IV8CachedObject> sp_v8_cached_object;
  RETURN_IF_FAIL(GetCachedObject(context_object, &sp_v8_cached_object));

  auto enumerator{WRL::Make<V8ObjectKeyEnumerator>(sp_v8_cached_object)};
  *pp_enumerator = enumerator.Detach();
  return S_OK;
}

IFACEMETHODIMP V8LocalValueProperty::GetValue(
    PCWSTR pwsz_key, IModelObject* p_v8_local_instance,
    IModelObject** pp_value) noexcept {
  // Get the parametric type within v8::Local<*>
  // Set value to a pointer to an instance of this type.

  WRL::ComPtr<IDebugHostType> sp_type;
  RETURN_IF_FAIL(p_v8_local_instance->GetTypeInfo(&sp_type));

  bool is_generic;
  RETURN_IF_FAIL(sp_type->IsGeneric(&is_generic));
  if (!is_generic) return E_FAIL;

  WRL::ComPtr<IDebugHostSymbol> sp_generic_arg;
  RETURN_IF_FAIL(sp_type->GetGenericArgumentAt(0, &sp_generic_arg));

  _bstr_t generic_name;
  RETURN_IF_FAIL(sp_generic_arg->GetName(generic_name.GetAddress()));

  WRL::ComPtr<IDebugHostContext> sp_ctx;
  RETURN_IF_FAIL(p_v8_local_instance->GetContext(&sp_ctx));

  Location loc;
  RETURN_IF_FAIL(p_v8_local_instance->GetLocation(&loc));

  // Read the pointer at the Object location
  ULONG64 obj_address;
  RETURN_IF_FAIL(
      sp_debug_host_memory->ReadPointers(sp_ctx.Get(), loc, 1, &obj_address));

  // If the val_ is a nullptr, then there is no value in the Local.
  if (obj_address == 0) {
    RETURN_IF_FAIL(CreateString(std::u16string{u"<empty>"}, pp_value));
  } else {
    // Get the corresponding Tagged<T> type for the generic_name found above.
    std::string narrow_tagged_name = std::string("v8::internal::Tagged<") +
                                     static_cast<const char*>(generic_name) +
                                     ">";
    std::u16string tagged_type_name = ConvertToU16String(narrow_tagged_name);
    WRL::ComPtr<IDebugHostType> tagged_type =
        Extension::Current()->GetTypeFromV8Module(sp_ctx,
                                                  tagged_type_name.c_str());
    if (tagged_type == nullptr) {
      // If we couldn't find the specific tagged type, try to find
      // Tagged<Object> instead.
      tagged_type = Extension::Current()->GetV8TaggedObjectType(sp_ctx);
    }

    // Create the result.
    RETURN_IF_FAIL(sp_data_model_manager->CreateTypedObject(
        sp_ctx.Get(), obj_address, tagged_type.Get(), pp_value));
  }

  return S_OK;
}

IFACEMETHODIMP V8LocalValueProperty::SetValue(
    PCWSTR /*pwsz_key*/, IModelObject* /*p_process_instance*/,
    IModelObject* /*p_value*/) noexcept {
  return E_NOTIMPL;
}

IFACEMETHODIMP V8InternalCompilerNodeIdProperty::GetValue(
    PCWSTR pwsz_key, IModelObject* p_v8_compiler_node_instance,
    IModelObject** pp_value) noexcept {
  WRL::ComPtr<IModelObject> sp_bit_field;
  RETURN_IF_FAIL(p_v8_compiler_node_instance->GetRawValue(
      SymbolKind::SymbolField, L"bit_field_", RawSearchNone, &sp_bit_field));

  uint64_t bit_field_value;
  RETURN_IF_FAIL(
      UnboxULong64(sp_bit_field.Get(), &bit_field_value, true /*convert*/));

  WRL::ComPtr<IDebugHostContext> sp_host_context;
  RETURN_IF_FAIL(p_v8_compiler_node_instance->GetContext(&sp_host_context));

  WRL::ComPtr<IDebugHostType> sp_id_field_type;
  RETURN_IF_FAIL(Extension::Current()
                     ->GetV8Module(sp_host_context)
                     ->FindTypeByName(L"v8::internal::compiler::Node::IdField",
                                      &sp_id_field_type));

  // Get 2nd template parameter as 24 in class.
  // v8::base::BitField<v8::internal::compiler::NodeId, 0, 24>.
  bool is_generic;
  RETURN_IF_FAIL(sp_id_field_type->IsGeneric(&is_generic));
  if (!is_generic) return E_FAIL;

  WRL::ComPtr<IDebugHostSymbol> sp_k_size_arg;
  RETURN_IF_FAIL(sp_id_field_type->GetGenericArgumentAt(2, &sp_k_size_arg));

  WRL::ComPtr<IDebugHostConstant> sp_k_size_constant;
  RETURN_IF_FAIL(sp_k_size_arg.As(&sp_k_size_constant));

  int k_size;
  RETURN_IF_FAIL(GetInt32(sp_k_size_constant.Get(), &k_size));

  // Compute node_id.
  uint32_t node_id = bit_field_value & (0xFFFFFFFF >> k_size);
  RETURN_IF_FAIL(CreateUInt32(node_id, pp_value));

  return S_OK;
}

IFACEMETHODIMP V8InternalCompilerNodeIdProperty::SetValue(
    PCWSTR /*pwsz_key*/, IModelObject* /*p_process_instance*/,
    IModelObject* /*p_value*/) noexcept {
  return E_NOTIMPL;
}

IFACEMETHODIMP V8InternalCompilerBitsetNameProperty::GetValue(
    PCWSTR pwsz_key, IModelObject* p_v8_compiler_type_instance,
    IModelObject** pp_value) noexcept {
  WRL::ComPtr<IModelObject> sp_payload;
  RETURN_IF_FAIL(p_v8_compiler_type_instance->GetRawValue(
      SymbolKind::SymbolField, L"payload_", RawSearchNone, &sp_payload));

  uint64_t payload_value;
  RETURN_IF_FAIL(
      UnboxULong64(sp_payload.Get(), &payload_value, true /*convert*/));

  const char* bitset_name = ::BitsetName(payload_value);
  if (!bitset_name) return E_FAIL;
  std::string name(bitset_name);
  RETURN_IF_FAIL(CreateString(ConvertToU16String(name), pp_value));

  return S_OK;
}

IFACEMETHODIMP V8InternalCompilerBitsetNameProperty::SetValue(
    PCWSTR /*pwsz_key*/, IModelObject* /*p_process_instance*/,
    IModelObject* /*p_value*/) noexcept {
  return E_NOTIMPL;
}

constexpr wchar_t usage[] =
    LR"(Invalid arguments.
First argument should be a uint64 representing the tagged value to investigate.
Second argument is optional, and may be a fully-qualified type name such as
v8::internal::String.)";

IFACEMETHODIMP InspectV8ObjectMethod::Call(IModelObject* p_context_object,
                                           ULONG64 arg_count,
                                           _In_reads_(arg_count)
                                               IModelObject** pp_arguments,
                                           IModelObject** pp_result,
                                           IKeyStore** pp_metadata) noexcept {
  // Read the arguments.
  ULONG64 tagged_value;
  _bstr_t type_name;
  if (arg_count < 1 ||
      FAILED(UnboxULong64(pp_arguments[0], &tagged_value, /*convert=*/true)) ||
      (arg_count >= 2 &&
       FAILED(UnboxString(pp_arguments[1], type_name.GetAddress())))) {
    sp_data_model_manager->CreateErrorObject(E_INVALIDARG, usage, pp_result);
    return E_INVALIDARG;
  }

  WRL::ComPtr<IDebugHostContext> sp_ctx;
  RETURN_IF_FAIL(sp_debug_host->GetCurrentContext(&sp_ctx));

  // We can't use CreateTypedObject for a value which may not actually reside
  // anywhere in memory, so create a synthetic object.
  WRL::ComPtr<V8CachedObject> cached_object =
      WRL::Make<V8CachedObject>(::GetHeapObject(
          sp_ctx, tagged_value, 0, static_cast<const char*>(type_name),
          /*is_compressed=*/false));
  return CreateSyntheticObjectForV8Object(sp_ctx.Get(), cached_object.Get(),
                                          pp_result);
}

// Creates an IModelObject representing the data in the given property.
HRESULT GetModelForProperty(const Property& prop,
                            WRL::ComPtr<IDebugHostContext>& sp_ctx,
                            IModelObject** result) {
  switch (prop.type) {
    case PropertyType::kPointer:
      return GetModelForBasicField(prop.addr_value, prop.type_name, sp_ctx,
                                   result);
    case PropertyType::kStruct:
      return GetModelForStruct(prop.addr_value, prop.fields, sp_ctx, result);
    case PropertyType::kArray:
    case PropertyType::kStructArray:
      // We can't currently look up types for TaggedMember and must use custom
      // arrays; see comments in GetModelForBasicField for more details.
      if (prop.type == PropertyType::kArray &&
          !TryUnwrapTaggedMemberType(prop.type_name, nullptr)) {
        // An array of things that are not structs or compressed tagged values
        // is most cleanly represented by a native array.
        return GetModelForNativeArray(prop.addr_value, prop.type_name,
                                      prop.length, sp_ctx, result);
      }
      // Otherwise, we must construct a custom iterable object.
      return GetModelForCustomArray(prop, sp_ctx, result);
    default:
      return E_FAIL;
  }
}

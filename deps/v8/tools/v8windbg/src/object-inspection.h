// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TOOLS_V8WINDBG_SRC_OBJECT_INSPECTION_H_
#define V8_TOOLS_V8WINDBG_SRC_OBJECT_INSPECTION_H_

#include <comutil.h>
#include <wrl/implements.h>

#include <sstream>
#include <string>
#include <vector>

#include "tools/v8windbg/base/dbgext.h"
#include "tools/v8windbg/src/v8-debug-helper-interop.h"
#include "tools/v8windbg/src/v8windbg-extension.h"

// The representation of the underlying V8 object that will be cached on the
// DataModel representation. (Needs to implement IUnknown).
class __declspec(uuid("6392E072-37BB-4220-A5FF-114098923A02")) IV8CachedObject
    : public IUnknown {
 public:
  virtual HRESULT __stdcall GetCachedV8HeapObject(
      V8HeapObject** pp_heap_object) = 0;
};

class V8CachedObject
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IV8CachedObject> {
 public:
  V8CachedObject(Location location, std::string uncompressed_type_name,
                 WRL::ComPtr<IDebugHostContext> context, bool is_compressed);
  V8CachedObject(V8HeapObject heap_object);
  ~V8CachedObject() override;

  static HRESULT Create(IModelObject* p_v8_object_instance,
                        IV8CachedObject** result);

  IFACEMETHOD(GetCachedV8HeapObject)(V8HeapObject** pp_heap_object);

 private:
  // The properties and description of the object, if already read.
  V8HeapObject heap_object_;
  bool heap_object_initialized_ = false;

  // Data that is necessary for reading the object.
  Location location_;
  std::string uncompressed_type_name_;
  WRL::ComPtr<IDebugHostContext> context_;
  bool is_compressed_ = false;
};

// A simple COM wrapper class to hold data required for IndexedFieldParent.
// (Needs to implement IUnknown).
class __declspec(uuid("6392E072-37BB-4220-A5FF-114098923A03")) IIndexedFieldData
    : public IUnknown {
 public:
  // Get a pointer to the Property object held by this IIndexedFieldData. The
  // pointer returned in this way is valid only while the containing
  // IIndexedFieldData is alive.
  virtual HRESULT __stdcall GetProperty(Property** property) = 0;
};

class IndexedFieldData
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IIndexedFieldData> {
 public:
  IndexedFieldData(Property property);
  ~IndexedFieldData() override;

  // Get a pointer to the Property object held by this IndexedFieldData. The
  // pointer returned in this way is valid only while the containing
  // IndexedFieldData is alive.
  IFACEMETHOD(GetProperty)(Property** property);

 private:
  Property property_;
};

// A parent model that provides indexing support for fields that contain arrays
// of something more complicated than basic native types.
class IndexedFieldParent
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IDataModelConcept, IIterableConcept, IIndexableConcept> {
 public:
  // IDataModelConcept
  IFACEMETHOD(InitializeObject)
  (IModelObject* model_object, IDebugHostTypeSignature* matching_type_signature,
   IDebugHostSymbolEnumerator* wildcard_matches);

  // IDataModelConcept
  IFACEMETHOD(GetName)(BSTR* model_name);

  // IIndexableConcept
  IFACEMETHOD(GetAt)
  (IModelObject* context_object, ULONG64 indexer_count, IModelObject** indexers,
   _COM_Errorptr_ IModelObject** object, IKeyStore** metadata);

  // IIndexableConcept
  IFACEMETHOD(GetDimensionality)
  (IModelObject* context_object, ULONG64* dimensionality);

  // IIndexableConcept
  IFACEMETHOD(SetAt)
  (IModelObject* context_object, ULONG64 indexer_count, IModelObject** indexers,
   IModelObject* value);

  // IIterableConcept
  IFACEMETHOD(GetDefaultIndexDimensionality)
  (IModelObject* context_object, ULONG64* dimensionality);

  // IIterableConcept
  IFACEMETHOD(GetIterator)
  (IModelObject* context_object, IModelIterator** iterator);
};

// An iterator for the values within an array field.
class IndexedFieldIterator
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelIterator> {
 public:
  IndexedFieldIterator(IModelObject* context_object);
  ~IndexedFieldIterator() override;

  IFACEMETHOD(Reset)();

  IFACEMETHOD(GetNext)
  (IModelObject** object, ULONG64 dimensions, IModelObject** indexers,
   IKeyStore** metadata);

 private:
  size_t next_ = 0;
  WRL::ComPtr<IModelObject> context_object_;
};

// Enumerates the names of fields on V8 objects.
class V8ObjectKeyEnumerator
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IKeyEnumerator> {
 public:
  V8ObjectKeyEnumerator(WRL::ComPtr<IV8CachedObject>& v8_cached_object);
  ~V8ObjectKeyEnumerator() override;

  IFACEMETHOD(Reset)();

  // This method will be called with a nullptr 'value' for each key if returned
  // from an IDynamicKeyProviderConcept. It will call GetKey on the
  // IDynamicKeyProviderConcept interface after each key returned.
  IFACEMETHOD(GetNext)(BSTR* key, IModelObject** value, IKeyStore** metadata);

 private:
  int index_ = 0;
  WRL::ComPtr<IV8CachedObject> sp_v8_cached_object_;
};

// A parent model for V8 handle types such as v8::internal::Handle<*>.
class V8LocalDataModel
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IDataModelConcept> {
 public:
  IFACEMETHOD(InitializeObject)
  (IModelObject* model_object, IDebugHostTypeSignature* matching_type_signature,
   IDebugHostSymbolEnumerator* wildcard_matches);

  IFACEMETHOD(GetName)(BSTR* model_name);
};

// A parent model for V8 object types such as v8::internal::Object.
class V8ObjectDataModel
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IDataModelConcept, IStringDisplayableConcept,
          IDynamicKeyProviderConcept> {
 public:
  HRESULT GetCachedObject(IModelObject* context_object,
                          IV8CachedObject** result) {
    // Get the IModelObject for this parent object. As it is a dynamic provider,
    // there is only one parent directly on the object.
    WRL::ComPtr<IModelObject> sp_parent_model, sp_context_adjuster;
    RETURN_IF_FAIL(context_object->GetParentModel(0, &sp_parent_model,
                                                  &sp_context_adjuster));

    // See if the cached object is already present
    WRL::ComPtr<IUnknown> sp_context;
    HRESULT status = context_object->GetContextForDataModel(
        sp_parent_model.Get(), &sp_context);

    WRL::ComPtr<IV8CachedObject> sp_v8_cached_object;

    if (SUCCEEDED(status)) {
      RETURN_IF_FAIL(sp_context.As(&sp_v8_cached_object));
    } else {
      RETURN_IF_FAIL(
          V8CachedObject::Create(context_object, &sp_v8_cached_object));
      RETURN_IF_FAIL(sp_v8_cached_object.As(&sp_context));
      RETURN_IF_FAIL(context_object->SetContextForDataModel(
          sp_parent_model.Get(), sp_context.Get()));
    }

    *result = sp_v8_cached_object.Detach();
    return S_OK;
  }

  IFACEMETHOD(InitializeObject)
  (IModelObject* model_object, IDebugHostTypeSignature* matching_type_signature,
   IDebugHostSymbolEnumerator* wildcard_matches);

  IFACEMETHOD(GetName)(BSTR* model_name);

  IFACEMETHOD(ToDisplayString)
  (IModelObject* context_object, IKeyStore* metadata, BSTR* display_string);

  // IDynamicKeyProviderConcept
  IFACEMETHOD(GetKey)
  (IModelObject* context_object, PCWSTR key, IModelObject** key_value,
   IKeyStore** metadata, bool* has_key);

  IFACEMETHOD(SetKey)
  (IModelObject* context_object, PCWSTR key, IModelObject* key_value,
   IKeyStore* metadata);

  IFACEMETHOD(EnumerateKeys)
  (IModelObject* context_object, IKeyEnumerator** pp_enumerator);
};

// The implemention of the "Value" getter for V8 handle types.
class V8LocalValueProperty
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelPropertyAccessor> {
 public:
  IFACEMETHOD(GetValue)
  (PCWSTR pwsz_key, IModelObject* p_v8_object_instance,
   IModelObject** pp_value);

  IFACEMETHOD(SetValue)
  (PCWSTR /*pwsz_key*/, IModelObject* /*p_process_instance*/,
   IModelObject* /*p_value*/);
};

// The implemention of the "NodeId" getter for v8::internal::compiler::Node
// type.
class V8InternalCompilerNodeIdProperty
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelPropertyAccessor> {
 public:
  IFACEMETHOD(GetValue)
  (PCWSTR pwsz_key, IModelObject* p_v8_object_instance,
   IModelObject** pp_value);

  IFACEMETHOD(SetValue)
  (PCWSTR /*pwsz_key*/, IModelObject* /*p_process_instance*/,
   IModelObject* /*p_value*/);
};

// The implemention of the "bitset_name" getter for v8::internal::compiler::Type
// type.
class V8InternalCompilerBitsetNameProperty
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelPropertyAccessor> {
 public:
  IFACEMETHOD(GetValue)
  (PCWSTR pwsz_key, IModelObject* p_v8_compiler_type_instance,
   IModelObject** pp_value);

  IFACEMETHOD(SetValue)
  (PCWSTR /*pwsz_key*/, IModelObject* /*p_process_instance*/,
   IModelObject* /*p_value*/);
};

// A way that someone can directly inspect a tagged value, even if that value
// isn't in memory (from a register, or the user's imagination, etc.).
class InspectV8ObjectMethod
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelMethod> {
 public:
  IFACEMETHOD(Call)
  (IModelObject* p_context_object, ULONG64 arg_count,
   _In_reads_(arg_count) IModelObject** pp_arguments, IModelObject** pp_result,
   IKeyStore** pp_metadata);
};

HRESULT GetModelForProperty(const Property& prop,
                            WRL::ComPtr<IDebugHostContext>& sp_ctx,
                            IModelObject** result);

#endif  // V8_TOOLS_V8WINDBG_SRC_OBJECT_INSPECTION_H_

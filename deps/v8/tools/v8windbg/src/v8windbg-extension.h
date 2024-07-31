// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TOOLS_V8WINDBG_SRC_V8WINDBG_EXTENSION_H_
#define V8_TOOLS_V8WINDBG_SRC_V8WINDBG_EXTENSION_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "tools/v8windbg/base/utilities.h"

// Responsible for initializing and uninitializing the extension. Also provides
// various convenience functions.
class Extension {
 public:
  Extension();
  HRESULT Initialize();
  ~Extension();
  WRL::ComPtr<IDebugHostModule> GetV8Module(
      WRL::ComPtr<IDebugHostContext>& sp_ctx);
  WRL::ComPtr<IDebugHostType> GetTypeFromV8Module(
      WRL::ComPtr<IDebugHostContext>& sp_ctx, const char16_t* type_name);
  WRL::ComPtr<IDebugHostType> GetV8TaggedObjectType(
      WRL::ComPtr<IDebugHostContext>& sp_ctx);
  void TryRegisterType(WRL::ComPtr<IDebugHostType>& sp_type,
                       std::u16string type_name);
  static Extension* Current() { return current_extension_.get(); }
  static void SetExtension(std::unique_ptr<Extension> new_extension) {
    current_extension_ = std::move(new_extension);
  }

  // Returns the parent model for instances of v8::internal::Object and similar
  // classes, which contain as their first and only field a tagged V8 value.
  IModelObject* GetObjectDataModel() { return sp_object_data_model_.Get(); }

  // Returns the parent model that provides indexing support for fields that
  // contain arrays of something more complicated than basic native types.
  IModelObject* GetIndexedFieldDataModel() {
    return sp_indexed_field_model_.Get();
  }

 private:
  HRESULT OverrideLocalsGetter(IModelObject* parent, const wchar_t* key_name,
                               bool is_parameters);

  template <class PropertyClass>
  HRESULT RegisterAndAddPropertyForClass(
      const wchar_t* class_name, const wchar_t* property_name,
      WRL::ComPtr<IModelObject> sp_data_model);

  // A property that has been overridden by this extension. The original value
  // must be put back in place during ~Extension.
  struct PropertyOverride {
    PropertyOverride();
    PropertyOverride(IModelObject* parent, std::u16string key_name,
                     IModelObject* original_value,
                     IKeyStore* original_metadata);
    ~PropertyOverride();
    PropertyOverride(const PropertyOverride&);
    PropertyOverride& operator=(const PropertyOverride&);
    WRL::ComPtr<IModelObject> parent;
    std::u16string key_name;
    WRL::ComPtr<IModelObject> original_value;
    WRL::ComPtr<IKeyStore> original_metadata;
  };

  struct RegistrationType {
    RegistrationType();
    RegistrationType(IDebugHostTypeSignature* sp_signature,
                     IModelObject* sp_data_model);
    ~RegistrationType();
    RegistrationType(const RegistrationType&);
    RegistrationType& operator=(const RegistrationType&);

    WRL::ComPtr<IDebugHostTypeSignature> sp_signature;
    WRL::ComPtr<IModelObject> sp_data_model;
  };

  static std::unique_ptr<Extension> current_extension_;

  WRL::ComPtr<IModelObject> sp_object_data_model_;
  WRL::ComPtr<IModelObject> sp_local_data_model_;
  WRL::ComPtr<IModelObject> sp_compiler_node_data_model_;
  WRL::ComPtr<IModelObject> sp_compiler_type_data_model_;
  WRL::ComPtr<IModelObject> sp_indexed_field_model_;

  WRL::ComPtr<IDebugHostModule> sp_v8_module_;
  std::unordered_map<std::u16string, WRL::ComPtr<IDebugHostType>>
      cached_v8_module_types_;
  std::vector<RegistrationType> registered_types_;
  std::vector<PropertyOverride> overridden_properties_;
  WRL::ComPtr<IDebugHostContext> sp_v8_module_ctx_;
  ULONG v8_module_proc_id_;
};
#endif  // V8_TOOLS_V8WINDBG_SRC_V8WINDBG_EXTENSION_H_

// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-module.h"

#include <functional>
#include <memory>

#include "src/api/api-inl.h"
#include "src/compiler/wasm-compiler.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/objects.h"
#include "src/wasm/jump-table-assembler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-init-expr.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module-builder.h"  // For {ZoneBuffer}.
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-result.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8::internal::wasm {

// Ensure that the max subtyping depth can be stored in the TypeDefinition.
static_assert(
    kV8MaxRttSubtypingDepth <=
    std::numeric_limits<decltype(TypeDefinition().subtyping_depth)>::max());

// static
int WasmMemory::GetMemory64GuardsShift(uint64_t max_memory_size) {
  // For memory64 we need a guard region that is at least twice the size of the
  // maximum size of the Wasm memory. In order to speed-up bounds checks, we
  // allocate the greater power-of-two size.
  DCHECK_NE(max_memory_size, 0U);
  size_t min_guards_size = 2 * max_memory_size;
  int guards_shift = 63 - base::bits::CountLeadingZeros64(min_guards_size);
  DCHECK_GE(guards_shift, 0);
  if (!base::bits::IsPowerOfTwo(min_guards_size)) {
    guards_shift++;
  }
  return guards_shift;
}

template <class Value>
void AdaptiveMap<Value>::FinishInitialization() {
  uint32_t count = 0;
  uint32_t max = 0;
  DCHECK_EQ(mode_, kInitializing);
  for (const auto& entry : *map_) {
    count++;
    max = std::max(max, entry.first);
  }
  if (count >= (max + 1) / kLoadFactor) {
    mode_ = kDense;
    vector_.resize(max + 1);
    for (auto& entry : *map_) {
      vector_[entry.first] = std::move(entry.second);
    }
    map_.reset();
  } else {
    mode_ = kSparse;
  }
}
template void NameMap::FinishInitialization();
template void IndirectNameMap::FinishInitialization();

WireBytesRef LazilyGeneratedNames::LookupFunctionName(
    ModuleWireBytes wire_bytes, uint32_t function_index) {
  base::MutexGuard lock(&mutex_);
  if (!has_functions_) {
    has_functions_ = true;
    DecodeFunctionNames(wire_bytes.module_bytes(), function_names_);
  }
  const WireBytesRef* result = function_names_.Get(function_index);
  if (!result) return WireBytesRef();
  return *result;
}

bool LazilyGeneratedNames::Has(uint32_t function_index) {
  DCHECK(has_functions_);
  base::MutexGuard lock(&mutex_);
  return function_names_.Get(function_index) != nullptr;
}

// static
int GetWasmFunctionOffset(const WasmModule* module, uint32_t func_index) {
  const std::vector<WasmFunction>& functions = module->functions;
  if (static_cast<uint32_t>(func_index) >= functions.size()) return -1;
  DCHECK_GE(kMaxInt, functions[func_index].code.offset());
  return static_cast<int>(functions[func_index].code.offset());
}

// static
int GetNearestWasmFunction(const WasmModule* module, uint32_t byte_offset) {
  const std::vector<WasmFunction>& functions = module->functions;

  // Binary search for a function containing the given position.
  int left = 0;                                    // inclusive
  int right = static_cast<int>(functions.size());  // exclusive
  if (right == 0) return -1;
  while (right - left > 1) {
    int mid = left + (right - left) / 2;
    if (functions[mid].code.offset() <= byte_offset) {
      left = mid;
    } else {
      right = mid;
    }
  }

  return left;
}

// static
int GetContainingWasmFunction(const WasmModule* module, uint32_t byte_offset) {
  int func_index = GetNearestWasmFunction(module, byte_offset);

  if (func_index >= 0) {
    // If the found function does not contain the given position, return -1.
    const WasmFunction& func = module->functions[func_index];
    if (byte_offset < func.code.offset() ||
        byte_offset >= func.code.end_offset()) {
      return -1;
    }
  }
  return func_index;
}

int GetSubtypingDepth(const WasmModule* module, uint32_t type_index) {
  DCHECK_LT(type_index, module->types.size());
  int depth = module->types[type_index].subtyping_depth;
  DCHECK_LE(depth, kV8MaxRttSubtypingDepth);
  return depth;
}

void LazilyGeneratedNames::AddForTesting(int function_index,
                                         WireBytesRef name) {
  base::MutexGuard lock(&mutex_);
  function_names_.Put(function_index, name);
}

AsmJsOffsetInformation::AsmJsOffsetInformation(
    base::Vector<const uint8_t> encoded_offsets)
    : encoded_offsets_(base::OwnedVector<const uint8_t>::Of(encoded_offsets)) {}

AsmJsOffsetInformation::~AsmJsOffsetInformation() = default;

int AsmJsOffsetInformation::GetSourcePosition(int declared_func_index,
                                              int byte_offset,
                                              bool is_at_number_conversion) {
  EnsureDecodedOffsets();

  DCHECK_LE(0, declared_func_index);
  DCHECK_GT(decoded_offsets_->functions.size(), declared_func_index);
  std::vector<AsmJsOffsetEntry>& function_offsets =
      decoded_offsets_->functions[declared_func_index].entries;

  auto byte_offset_less = [](const AsmJsOffsetEntry& a,
                             const AsmJsOffsetEntry& b) {
    return a.byte_offset < b.byte_offset;
  };
  SLOW_DCHECK(std::is_sorted(function_offsets.begin(), function_offsets.end(),
                             byte_offset_less));
  // If there are no positions recorded, map offset 0 (for function entry) to
  // position 0.
  if (function_offsets.empty() && byte_offset == 0) return 0;
  auto it =
      std::lower_bound(function_offsets.begin(), function_offsets.end(),
                       AsmJsOffsetEntry{byte_offset, 0, 0}, byte_offset_less);
  DCHECK_NE(function_offsets.end(), it);
  DCHECK_EQ(byte_offset, it->byte_offset);
  return is_at_number_conversion ? it->source_position_number_conversion
                                 : it->source_position_call;
}

std::pair<int, int> AsmJsOffsetInformation::GetFunctionOffsets(
    int declared_func_index) {
  EnsureDecodedOffsets();

  DCHECK_LE(0, declared_func_index);
  DCHECK_GT(decoded_offsets_->functions.size(), declared_func_index);
  AsmJsOffsetFunctionEntries& function_info =
      decoded_offsets_->functions[declared_func_index];

  return {function_info.start_offset, function_info.end_offset};
}

void AsmJsOffsetInformation::EnsureDecodedOffsets() {
  base::MutexGuard mutex_guard(&mutex_);
  DCHECK_EQ(encoded_offsets_ == nullptr, decoded_offsets_ != nullptr);

  if (decoded_offsets_) return;
  AsmJsOffsetsResult result =
      wasm::DecodeAsmJsOffsets(encoded_offsets_.as_vector());
  decoded_offsets_ = std::make_unique<AsmJsOffsets>(std::move(result).value());
  encoded_offsets_.ReleaseData();
}

// Get a string stored in the module bytes representing a name.
WasmName ModuleWireBytes::GetNameOrNull(WireBytesRef ref) const {
  if (!ref.is_set()) return {nullptr, 0};  // no name.
  DCHECK(BoundsCheck(ref));
  return WasmName::cast(
      module_bytes_.SubVector(ref.offset(), ref.end_offset()));
}

// Get a string stored in the module bytes representing a function name.
WasmName ModuleWireBytes::GetNameOrNull(int func_index,
                                        const WasmModule* module) const {
  return GetNameOrNull(
      module->lazily_generated_names.LookupFunctionName(*this, func_index));
}

std::ostream& operator<<(std::ostream& os, const WasmFunctionName& name) {
  os << "#" << name.func_index_;
  if (!name.name_.empty()) {
    if (name.name_.begin()) {
      os << ":";
      os.write(name.name_.begin(), name.name_.length());
    }
  } else {
    os << "?";
  }
  return os;
}

WasmModule::WasmModule(ModuleOrigin origin)
    : signature_zone(GetWasmEngine()->allocator(), "signature zone"),
      origin(origin) {}

bool IsWasmCodegenAllowed(Isolate* isolate, Handle<NativeContext> context) {
  // TODO(wasm): Once wasm has its own CSP policy, we should introduce a
  // separate callback that includes information about the module about to be
  // compiled. For the time being, pass an empty string as placeholder for the
  // sources.
  if (auto wasm_codegen_callback = isolate->allow_wasm_code_gen_callback()) {
    return wasm_codegen_callback(
        v8::Utils::ToLocal(context),
        v8::Utils::ToLocal(isolate->factory()->empty_string()));
  }
  auto codegen_callback = isolate->allow_code_gen_callback();
  return codegen_callback == nullptr ||
         codegen_callback(
             v8::Utils::ToLocal(context),
             v8::Utils::ToLocal(isolate->factory()->empty_string()));
}

DirectHandle<String> ErrorStringForCodegen(Isolate* isolate,
                                           DirectHandle<Context> context) {
  DirectHandle<Object> error = context->ErrorMessageForWasmCodeGeneration();
  DCHECK(!error.is_null());
  return Object::NoSideEffectsToString(isolate, error);
}

namespace {

// Converts the given {type} into a string representation that can be used in
// reflective functions. Should be kept in sync with the {GetValueType} helper.
Handle<String> ToValueTypeString(Isolate* isolate, ValueType type) {
  return isolate->factory()->InternalizeUtf8String(base::VectorOf(type.name()));
}
}  // namespace

Handle<JSObject> GetTypeForFunction(Isolate* isolate, const FunctionSig* sig,
                                    bool for_exception) {
  Factory* factory = isolate->factory();

  // Extract values for the {ValueType[]} arrays.
  int param_index = 0;
  int param_count = static_cast<int>(sig->parameter_count());
  DirectHandle<FixedArray> param_values = factory->NewFixedArray(param_count);
  for (ValueType type : sig->parameters()) {
    DirectHandle<String> type_value = ToValueTypeString(isolate, type);
    param_values->set(param_index++, *type_value);
  }

  // Create the resulting {FunctionType} object.
  Handle<JSFunction> object_function = isolate->object_function();
  Handle<JSObject> object = factory->NewJSObject(object_function);
  DirectHandle<JSArray> params = factory->NewJSArrayWithElements(param_values);
  Handle<String> params_string = factory->InternalizeUtf8String("parameters");
  Handle<String> results_string = factory->InternalizeUtf8String("results");
  JSObject::AddProperty(isolate, object, params_string, params, NONE);

  // Now add the result types if needed.
  if (for_exception) {
    DCHECK_EQ(sig->returns().size(), 0);
  } else {
    int result_index = 0;
    int result_count = static_cast<int>(sig->return_count());
    DirectHandle<FixedArray> result_values =
        factory->NewFixedArray(result_count);
    for (ValueType type : sig->returns()) {
      DirectHandle<String> type_value = ToValueTypeString(isolate, type);
      result_values->set(result_index++, *type_value);
    }
    DirectHandle<JSArray> results =
        factory->NewJSArrayWithElements(result_values);
    JSObject::AddProperty(isolate, object, results_string, results, NONE);
  }

  return object;
}

Handle<JSObject> GetTypeForGlobal(Isolate* isolate, bool is_mutable,
                                  ValueType type) {
  Factory* factory = isolate->factory();

  Handle<JSFunction> object_function = isolate->object_function();
  Handle<JSObject> object = factory->NewJSObject(object_function);
  Handle<String> mutable_string = factory->InternalizeUtf8String("mutable");
  Handle<String> value_string = factory->value_string();
  JSObject::AddProperty(isolate, object, mutable_string,
                        factory->ToBoolean(is_mutable), NONE);
  JSObject::AddProperty(isolate, object, value_string,
                        ToValueTypeString(isolate, type), NONE);

  return object;
}

Handle<JSObject> GetTypeForMemory(Isolate* isolate, uint32_t min_size,
                                  std::optional<uint32_t> max_size, bool shared,
                                  bool is_memory64) {
  Factory* factory = isolate->factory();

  Handle<JSFunction> object_function = isolate->object_function();
  Handle<JSObject> object = factory->NewJSObject(object_function);
  Handle<String> minimum_string = factory->InternalizeUtf8String("minimum");
  Handle<String> maximum_string = factory->InternalizeUtf8String("maximum");
  Handle<String> shared_string = factory->InternalizeUtf8String("shared");
  Handle<String> index_string = factory->InternalizeUtf8String("index");
  JSObject::AddProperty(isolate, object, minimum_string,
                        factory->NewNumberFromUint(min_size), NONE);
  if (max_size.has_value()) {
    JSObject::AddProperty(isolate, object, maximum_string,
                          factory->NewNumberFromUint(max_size.value()), NONE);
  }
  JSObject::AddProperty(isolate, object, shared_string,
                        factory->ToBoolean(shared), NONE);

  auto index = is_memory64 ? "i64" : "i32";
  JSObject::AddProperty(isolate, object, index_string,
                        factory->InternalizeUtf8String(index), NONE);

  return object;
}

Handle<JSObject> GetTypeForTable(Isolate* isolate, ValueType type,
                                 uint32_t min_size,
                                 std::optional<uint32_t> max_size,
                                 bool is_table64) {
  Factory* factory = isolate->factory();

  DirectHandle<String> element =
      factory->InternalizeUtf8String(base::VectorOf(type.name()));

  Handle<JSFunction> object_function = isolate->object_function();
  Handle<JSObject> object = factory->NewJSObject(object_function);
  Handle<String> element_string = factory->element_string();
  Handle<String> minimum_string = factory->InternalizeUtf8String("minimum");
  Handle<String> maximum_string = factory->InternalizeUtf8String("maximum");
  Handle<String> index_string = factory->InternalizeUtf8String("index");
  JSObject::AddProperty(isolate, object, element_string, element, NONE);
  JSObject::AddProperty(isolate, object, minimum_string,
                        factory->NewNumberFromUint(min_size), NONE);
  if (max_size.has_value()) {
    JSObject::AddProperty(isolate, object, maximum_string,
                          factory->NewNumberFromUint(max_size.value()), NONE);
  }
  auto index = is_table64 ? "i64" : "i32";
  JSObject::AddProperty(isolate, object, index_string,
                        factory->InternalizeUtf8String(index), NONE);

  return object;
}

Handle<JSArray> GetImports(Isolate* isolate,
                           DirectHandle<WasmModuleObject> module_object) {
  auto enabled_features = i::wasm::WasmEnabledFeatures::FromIsolate(isolate);
  Factory* factory = isolate->factory();

  Handle<String> module_string = factory->InternalizeUtf8String("module");
  Handle<String> name_string = factory->name_string();
  Handle<String> kind_string = factory->InternalizeUtf8String("kind");
  Handle<String> type_string = factory->InternalizeUtf8String("type");

  Handle<String> function_string = factory->function_string();
  Handle<String> table_string = factory->InternalizeUtf8String("table");
  Handle<String> memory_string = factory->InternalizeUtf8String("memory");
  Handle<String> global_string = factory->global_string();
  Handle<String> tag_string = factory->InternalizeUtf8String("tag");

  // Create the result array.
  NativeModule* native_module = module_object->native_module();
  const WasmModule* module = native_module->module();
  int num_imports = static_cast<int>(module->import_table.size());
  Handle<JSArray> array_object = factory->NewJSArray(PACKED_ELEMENTS, 0, 0);
  Handle<FixedArray> storage = factory->NewFixedArray(num_imports);
  JSArray::SetContent(array_object, storage);

  Handle<JSFunction> object_function =
      Handle<JSFunction>(isolate->native_context()->object_function(), isolate);

  // Populate the result array.
  const WellKnownImportsList& well_known_imports =
      module->type_feedback.well_known_imports;
  const std::string& magic_string_constants =
      native_module->compile_imports().constants_module();
  const bool has_magic_string_constants =
      native_module->compile_imports().contains(
          CompileTimeImport::kStringConstants);

  int cursor = 0;
  for (int index = 0; index < num_imports; ++index) {
    const WasmImport& import = module->import_table[index];

    Handle<JSObject> entry = factory->NewJSObject(object_function);

    Handle<String> import_kind;
    Handle<JSObject> type_value;
    switch (import.kind) {
      case kExternalFunction:
        if (IsCompileTimeImport(well_known_imports.get(import.index))) {
          continue;
        }
        if (enabled_features.has_type_reflection()) {
          auto& func = module->functions[import.index];
          type_value = GetTypeForFunction(isolate, func.sig);
        }
        import_kind = function_string;
        break;
      case kExternalTable:
        if (enabled_features.has_type_reflection()) {
          auto& table = module->tables[import.index];
          std::optional<uint32_t> maximum_size;
          if (table.has_maximum_size) maximum_size.emplace(table.maximum_size);
          type_value = GetTypeForTable(isolate, table.type, table.initial_size,
                                       maximum_size, table.is_table64);
        }
        import_kind = table_string;
        break;
      case kExternalMemory:
        if (enabled_features.has_type_reflection()) {
          auto& memory = module->memories[import.index];
          std::optional<uint32_t> maximum_size;
          if (memory.has_maximum_pages) {
            maximum_size.emplace(memory.maximum_pages);
          }
          type_value =
              GetTypeForMemory(isolate, memory.initial_pages, maximum_size,
                               memory.is_shared, memory.is_memory64);
        }
        import_kind = memory_string;
        break;
      case kExternalGlobal:
        if (has_magic_string_constants &&
            import.module_name.length() == magic_string_constants.size() &&
            std::equal(magic_string_constants.begin(),
                       magic_string_constants.end(),
                       module_object->native_module()->wire_bytes().begin() +
                           import.module_name.offset())) {
          continue;
        }
        if (enabled_features.has_type_reflection()) {
          auto& global = module->globals[import.index];
          type_value =
              GetTypeForGlobal(isolate, global.mutability, global.type);
        }
        import_kind = global_string;
        break;
      case kExternalTag:
        import_kind = tag_string;
        break;
    }
    DCHECK(!import_kind.is_null());

    DirectHandle<String> import_module =
        WasmModuleObject::ExtractUtf8StringFromModuleBytes(
            isolate, module_object, import.module_name, kInternalize);

    DirectHandle<String> import_name =
        WasmModuleObject::ExtractUtf8StringFromModuleBytes(
            isolate, module_object, import.field_name, kInternalize);

    JSObject::AddProperty(isolate, entry, module_string, import_module, NONE);
    JSObject::AddProperty(isolate, entry, name_string, import_name, NONE);
    JSObject::AddProperty(isolate, entry, kind_string, import_kind, NONE);
    if (!type_value.is_null()) {
      JSObject::AddProperty(isolate, entry, type_string, type_value, NONE);
    }

    storage->set(cursor++, *entry);
  }

  array_object->set_length(Smi::FromInt(cursor));
  return array_object;
}

Handle<JSArray> GetExports(Isolate* isolate,
                           DirectHandle<WasmModuleObject> module_object) {
  auto enabled_features = i::wasm::WasmEnabledFeatures::FromIsolate(isolate);
  Factory* factory = isolate->factory();

  Handle<String> name_string = factory->name_string();
  Handle<String> kind_string = factory->InternalizeUtf8String("kind");
  Handle<String> type_string = factory->InternalizeUtf8String("type");

  DirectHandle<String> function_string = factory->function_string();
  DirectHandle<String> table_string = factory->InternalizeUtf8String("table");
  DirectHandle<String> memory_string = factory->InternalizeUtf8String("memory");
  DirectHandle<String> global_string = factory->global_string();
  DirectHandle<String> tag_string = factory->InternalizeUtf8String("tag");

  // Create the result array.
  const WasmModule* module = module_object->module();
  int num_exports = static_cast<int>(module->export_table.size());
  Handle<JSArray> array_object = factory->NewJSArray(PACKED_ELEMENTS, 0, 0);
  Handle<FixedArray> storage = factory->NewFixedArray(num_exports);
  JSArray::SetContent(array_object, storage);
  array_object->set_length(Smi::FromInt(num_exports));

  Handle<JSFunction> object_function =
      Handle<JSFunction>(isolate->native_context()->object_function(), isolate);

  // Populate the result array.
  for (int index = 0; index < num_exports; ++index) {
    const WasmExport& exp = module->export_table[index];

    DirectHandle<String> export_kind;
    Handle<JSObject> type_value;
    switch (exp.kind) {
      case kExternalFunction:
        if (enabled_features.has_type_reflection()) {
          auto& func = module->functions[exp.index];
          type_value = GetTypeForFunction(isolate, func.sig);
        }
        export_kind = function_string;
        break;
      case kExternalTable:
        if (enabled_features.has_type_reflection()) {
          auto& table = module->tables[exp.index];
          std::optional<uint32_t> maximum_size;
          if (table.has_maximum_size) maximum_size.emplace(table.maximum_size);
          type_value = GetTypeForTable(isolate, table.type, table.initial_size,
                                       maximum_size, table.is_table64);
        }
        export_kind = table_string;
        break;
      case kExternalMemory:
        if (enabled_features.has_type_reflection()) {
          auto& memory = module->memories[exp.index];
          std::optional<uint32_t> maximum_size;
          if (memory.has_maximum_pages) {
            maximum_size.emplace(memory.maximum_pages);
          }
          type_value =
              GetTypeForMemory(isolate, memory.initial_pages, maximum_size,
                               memory.is_shared, memory.is_memory64);
        }
        export_kind = memory_string;
        break;
      case kExternalGlobal:
        if (enabled_features.has_type_reflection()) {
          auto& global = module->globals[exp.index];
          type_value =
              GetTypeForGlobal(isolate, global.mutability, global.type);
        }
        export_kind = global_string;
        break;
      case kExternalTag:
        export_kind = tag_string;
        break;
      default:
        UNREACHABLE();
    }

    Handle<JSObject> entry = factory->NewJSObject(object_function);

    DirectHandle<String> export_name =
        WasmModuleObject::ExtractUtf8StringFromModuleBytes(
            isolate, module_object, exp.name, kNoInternalize);

    JSObject::AddProperty(isolate, entry, name_string, export_name, NONE);
    JSObject::AddProperty(isolate, entry, kind_string, export_kind, NONE);
    if (!type_value.is_null()) {
      JSObject::AddProperty(isolate, entry, type_string, type_value, NONE);
    }

    storage->set(index, *entry);
  }

  return array_object;
}

Handle<JSArray> GetCustomSections(Isolate* isolate,
                                  DirectHandle<WasmModuleObject> module_object,
                                  DirectHandle<String> name,
                                  ErrorThrower* thrower) {
  Factory* factory = isolate->factory();

  base::Vector<const uint8_t> wire_bytes =
      module_object->native_module()->wire_bytes();
  std::vector<CustomSectionOffset> custom_sections =
      DecodeCustomSections(wire_bytes);

  std::vector<Handle<Object>> matching_sections;

  // Gather matching sections.
  for (auto& section : custom_sections) {
    DirectHandle<String> section_name =
        WasmModuleObject::ExtractUtf8StringFromModuleBytes(
            isolate, module_object, section.name, kNoInternalize);

    if (!name->Equals(*section_name)) continue;

    // Make a copy of the payload data in the section.
    size_t size = section.payload.length();
    MaybeHandle<JSArrayBuffer> result =
        isolate->factory()->NewJSArrayBufferAndBackingStore(
            size, InitializedFlag::kUninitialized);
    Handle<JSArrayBuffer> array_buffer;
    if (!result.ToHandle(&array_buffer)) {
      thrower->RangeError("out of memory allocating custom section data");
      return Handle<JSArray>();
    }
    memcpy(array_buffer->backing_store(),
           wire_bytes.begin() + section.payload.offset(),
           section.payload.length());

    matching_sections.push_back(array_buffer);
  }

  int num_custom_sections = static_cast<int>(matching_sections.size());
  Handle<JSArray> array_object = factory->NewJSArray(PACKED_ELEMENTS, 0, 0);
  Handle<FixedArray> storage = factory->NewFixedArray(num_custom_sections);
  JSArray::SetContent(array_object, storage);
  array_object->set_length(Smi::FromInt(num_custom_sections));

  for (int i = 0; i < num_custom_sections; i++) {
    storage->set(i, *matching_sections[i]);
  }

  return array_object;
}

// Get the source position from a given function index and wire bytes offset
// (relative to the function entry), for either asm.js or pure Wasm modules.
int GetSourcePosition(const WasmModule* module, uint32_t func_index,
                      uint32_t byte_offset, bool is_at_number_conversion) {
  DCHECK_EQ(is_asmjs_module(module),
            module->asm_js_offset_information != nullptr);
  if (!is_asmjs_module(module)) {
    // For non-asm.js modules, we just add the function's start offset
    // to make a module-relative position.
    return byte_offset + GetWasmFunctionOffset(module, func_index);
  }

  // asm.js modules have an additional offset table that must be searched.
  return module->asm_js_offset_information->GetSourcePosition(
      declared_function_index(module, func_index), byte_offset,
      is_at_number_conversion);
}

size_t WasmModule::EstimateStoredSize() const {
  UPDATE_WHEN_CLASS_CHANGES(WasmModule,
#if V8_ENABLE_DRUMBRAKE
                            920
#else   // V8_ENABLE_DRUMBRAKE
                            856
#endif  // V8_ENABLE_DRUMBRAKE
  );
  return sizeof(WasmModule) +                            // --
         signature_zone.allocation_size_for_tracing() +  // --
         ContentSize(types) +                            // --
         ContentSize(isorecursive_canonical_type_ids) +  // --
         ContentSize(functions) +                        // --
         ContentSize(globals) +                          // --
         ContentSize(data_segments) +                    // --
         ContentSize(tables) +                           // --
         ContentSize(memories) +                         // --
         ContentSize(import_table) +                     // --
         ContentSize(export_table) +                     // --
         ContentSize(tags) +                             // --
         ContentSize(stringref_literals) +               // --
         ContentSize(elem_segments) +                    // --
         ContentSize(compilation_hints) +                // --
         ContentSize(branch_hints) +                     // --
         ContentSize(inst_traces) +                      // --
         (num_declared_functions + 7) / 8;               // validated_functions
}

template <class Value>
size_t AdaptiveMap<Value>::EstimateCurrentMemoryConsumption() const {
  UNREACHABLE();  // Explicit implementations below.
}

template <>
size_t NameMap::EstimateCurrentMemoryConsumption() const {
  size_t result = ContentSize(vector_);
  if (map_) result += ContentSize(*map_);
  return result;
}

size_t LazilyGeneratedNames::EstimateCurrentMemoryConsumption() const {
  base::MutexGuard lock(&mutex_);
  return function_names_.EstimateCurrentMemoryConsumption();
}

template <>
size_t IndirectNameMap::EstimateCurrentMemoryConsumption() const {
  size_t result = ContentSize(vector_);
  for (const auto& inner_map : vector_) {
    result += inner_map.EstimateCurrentMemoryConsumption();
  }
  if (map_) {
    result += ContentSize(*map_);
    for (const auto& [outer_index, inner_map] : *map_) {
      result += inner_map.EstimateCurrentMemoryConsumption();
    }
  }
  return result;
}

size_t TypeFeedbackStorage::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(TypeFeedbackStorage, 200);
  UPDATE_WHEN_CLASS_CHANGES(FunctionTypeFeedback, 48);
  // Not including sizeof(TFS) because that's contained in sizeof(WasmModule).
  base::SharedMutexGuard<base::kShared> lock(&mutex);
  size_t result = ContentSize(feedback_for_function);
  for (const auto& [func_idx, feedback] : feedback_for_function) {
    result += ContentSize(feedback.feedback_vector);
    result += feedback.call_targets.size() * sizeof(uint32_t);
  }
  result += ContentSize(deopt_count_for_function);
  // The size of {well_known_imports} can only be estimated at the WasmModule
  // level.
  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("TypeFeedback: %zu\n", result);
  }
  return result;
}

size_t WasmModule::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(WasmModule,
#if V8_ENABLE_DRUMBRAKE
                            920
#else   // V8_ENABLE_DRUMBRAKE
                            856
#endif  // V8_ENABLE_DRUMBRAKE
  );
  size_t result = EstimateStoredSize();

  result += type_feedback.EstimateCurrentMemoryConsumption();
  // For type_feedback.well_known_imports:
  result += num_imported_functions * sizeof(WellKnownImport);

  result += lazily_generated_names.EstimateCurrentMemoryConsumption();

  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("WasmModule: %zu\n", result);
  }
  return result;
}

size_t PrintSignature(base::Vector<char> buffer, const wasm::FunctionSig* sig,
                      char delimiter) {
  if (buffer.empty()) return 0;
  size_t old_size = buffer.size();
  auto append_char = [&buffer](char c) {
    if (buffer.size() == 1) return;  // Keep last character for '\0'.
    buffer[0] = c;
    buffer += 1;
  };
  for (wasm::ValueType t : sig->parameters()) {
    append_char(t.short_name());
  }
  append_char(delimiter);
  for (wasm::ValueType t : sig->returns()) {
    append_char(t.short_name());
  }
  buffer[0] = '\0';
  return old_size - buffer.size();
}

int JumpTableOffset(const WasmModule* module, int func_index) {
  return JumpTableAssembler::JumpSlotIndexToOffset(
      declared_function_index(module, func_index));
}

size_t GetWireBytesHash(base::Vector<const uint8_t> wire_bytes) {
  return StringHasher::HashSequentialString(
      reinterpret_cast<const char*>(wire_bytes.begin()), wire_bytes.length(),
      kZeroHashSeed);
}

int NumFeedbackSlots(const WasmModule* module, int func_index) {
  base::SharedMutexGuard<base::kShared> type_feedback_guard{
      &module->type_feedback.mutex};
  auto it = module->type_feedback.feedback_for_function.find(func_index);
  if (it == module->type_feedback.feedback_for_function.end()) return 0;
  // The number of call instructions is capped by max function size.
  static_assert(kV8MaxWasmFunctionSize < std::numeric_limits<int>::max() / 2);
  return static_cast<int>(2 * it->second.call_targets.size());
}

}  // namespace v8::internal::wasm

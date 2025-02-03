// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_HANDLER_CONFIGURATION_INL_H_
#define V8_IC_HANDLER_CONFIGURATION_INL_H_

#include "src/builtins/builtins.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/ic/handler-configuration.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/field-index-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(LoadHandler, DataHandler)

// Decodes kind from Smi-handler.
LoadHandler::Kind LoadHandler::GetHandlerKind(Tagged<Smi> smi_handler) {
  return KindBits::decode(smi_handler.value());
}

Handle<Smi> LoadHandler::LoadNormal(Isolate* isolate) {
  int config = KindBits::encode(Kind::kNormal);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadGlobal(Isolate* isolate) {
  int config = KindBits::encode(Kind::kGlobal);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadInterceptor(Isolate* isolate) {
  int config = KindBits::encode(Kind::kInterceptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadSlow(Isolate* isolate) {
  int config = KindBits::encode(Kind::kSlow);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadField(Isolate* isolate, FieldIndex field_index) {
  int config = KindBits::encode(Kind::kField) |
               IsInobjectBits::encode(field_index.is_inobject()) |
               IsDoubleBits::encode(field_index.is_double()) |
               FieldIndexBits::encode(field_index.index());
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadWasmStructField(Isolate* isolate,
                                             WasmValueType type, int offset) {
  int config = KindBits::encode(Kind::kField) | IsWasmStructBits::encode(true) |
               WasmFieldTypeBits::encode(type) |
               WasmFieldOffsetBits::encode(offset);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadConstantFromPrototype(Isolate* isolate) {
  int config = KindBits::encode(Kind::kConstantFromPrototype);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadAccessorFromPrototype(Isolate* isolate) {
  int config = KindBits::encode(Kind::kAccessorFromPrototype);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadProxy(Isolate* isolate) {
  int config = KindBits::encode(Kind::kProxy);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadNativeDataProperty(Isolate* isolate,
                                                int descriptor) {
  int config = KindBits::encode(Kind::kNativeDataProperty) |
               DescriptorBits::encode(descriptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadApiGetter(Isolate* isolate,
                                       bool holder_is_receiver) {
  int config =
      KindBits::encode(holder_is_receiver ? Kind::kApiGetter
                                          : Kind::kApiGetterHolderIsPrototype);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadModuleExport(Isolate* isolate, int index) {
  int config =
      KindBits::encode(Kind::kModuleExport) | ExportsIndexBits::encode(index);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadNonExistent(Isolate* isolate) {
  int config = KindBits::encode(Kind::kNonExistent);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadElement(Isolate* isolate,
                                     ElementsKind elements_kind,
                                     bool is_js_array,
                                     KeyedAccessLoadMode load_mode) {
  DCHECK_IMPLIES(LoadModeHandlesHoles(load_mode),
                 IsHoleyElementsKind(elements_kind));
  int config = KindBits::encode(Kind::kElement) |
               AllowOutOfBoundsBits::encode(LoadModeHandlesOOB(load_mode)) |
               ElementsKindBits::encode(elements_kind) |
               AllowHandlingHole::encode(LoadModeHandlesHoles(load_mode)) |
               IsJsArrayBits::encode(is_js_array);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadIndexedString(Isolate* isolate,
                                           KeyedAccessLoadMode load_mode) {
  int config = KindBits::encode(Kind::kIndexedString) |
               AllowOutOfBoundsBits::encode(LoadModeHandlesOOB(load_mode));
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadWasmArrayElement(Isolate* isolate,
                                              WasmValueType type) {
  int config = KindBits::encode(Kind::kElement) |
               IsWasmArrayBits::encode(true) | WasmArrayTypeBits::encode(type);
  return handle(Smi::FromInt(config), isolate);
}

OBJECT_CONSTRUCTORS_IMPL(StoreHandler, DataHandler)

Handle<Smi> StoreHandler::StoreGlobalProxy(Isolate* isolate) {
  int config = KindBits::encode(Kind::kGlobalProxy);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreNormal(Isolate* isolate) {
  int config = KindBits::encode(Kind::kNormal);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreInterceptor(Isolate* isolate) {
  int config = KindBits::encode(Kind::kInterceptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Code> StoreHandler::StoreSloppyArgumentsBuiltin(
    Isolate* isolate, KeyedAccessStoreMode mode) {
  switch (mode) {
    case KeyedAccessStoreMode::kInBounds:
      return BUILTIN_CODE(isolate, KeyedStoreIC_SloppyArguments_InBounds);
    case KeyedAccessStoreMode::kGrowAndHandleCOW:
      return BUILTIN_CODE(
          isolate, KeyedStoreIC_SloppyArguments_NoTransitionGrowAndHandleCOW);
    case KeyedAccessStoreMode::kIgnoreTypedArrayOOB:
      return BUILTIN_CODE(
          isolate,
          KeyedStoreIC_SloppyArguments_NoTransitionIgnoreTypedArrayOOB);
    case KeyedAccessStoreMode::kHandleCOW:
      return BUILTIN_CODE(isolate,
                          KeyedStoreIC_SloppyArguments_NoTransitionHandleCOW);
    default:
      UNREACHABLE();
  }
}

Handle<Code> StoreHandler::StoreFastElementBuiltin(Isolate* isolate,
                                                   KeyedAccessStoreMode mode) {
  switch (mode) {
    case KeyedAccessStoreMode::kInBounds:
      return BUILTIN_CODE(isolate, StoreFastElementIC_InBounds);
    case KeyedAccessStoreMode::kGrowAndHandleCOW:
      return BUILTIN_CODE(isolate,
                          StoreFastElementIC_NoTransitionGrowAndHandleCOW);
    case KeyedAccessStoreMode::kIgnoreTypedArrayOOB:
      return BUILTIN_CODE(isolate,
                          StoreFastElementIC_NoTransitionIgnoreTypedArrayOOB);
    case KeyedAccessStoreMode::kHandleCOW:
      return BUILTIN_CODE(isolate, StoreFastElementIC_NoTransitionHandleCOW);
    default:
      UNREACHABLE();
  }
}

Handle<Code> StoreHandler::ElementsTransitionAndStoreBuiltin(
    Isolate* isolate, KeyedAccessStoreMode mode) {
  switch (mode) {
    case KeyedAccessStoreMode::kInBounds:
      return BUILTIN_CODE(isolate, ElementsTransitionAndStore_InBounds);
    case KeyedAccessStoreMode::kGrowAndHandleCOW:
      return BUILTIN_CODE(
          isolate, ElementsTransitionAndStore_NoTransitionGrowAndHandleCOW);
    case KeyedAccessStoreMode::kIgnoreTypedArrayOOB:
      return BUILTIN_CODE(
          isolate, ElementsTransitionAndStore_NoTransitionIgnoreTypedArrayOOB);
    case KeyedAccessStoreMode::kHandleCOW:
      return BUILTIN_CODE(isolate,
                          ElementsTransitionAndStore_NoTransitionHandleCOW);
    default:
      UNREACHABLE();
  }
}

Handle<Smi> StoreHandler::StoreSlow(Isolate* isolate,
                                    KeyedAccessStoreMode store_mode) {
  int config = KindBits::encode(Kind::kSlow) |
               KeyedAccessStoreModeBits::encode(store_mode);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreProxy(Isolate* isolate) {
  return handle(StoreProxy(), isolate);
}

Tagged<Smi> StoreHandler::StoreProxy() {
  int config = KindBits::encode(Kind::kProxy);
  return Smi::FromInt(config);
}

Handle<Smi> StoreHandler::StoreField(Isolate* isolate, Kind kind,
                                     int descriptor, FieldIndex field_index,
                                     Representation representation) {
  DCHECK(!representation.IsNone());
  DCHECK(kind == Kind::kField || kind == Kind::kConstField ||
         kind == Kind::kSharedStructField);

  int config = KindBits::encode(kind) |
               IsInobjectBits::encode(field_index.is_inobject()) |
               RepresentationBits::encode(representation.kind()) |
               DescriptorBits::encode(descriptor) |
               FieldIndexBits::encode(field_index.index());
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreField(Isolate* isolate, int descriptor,
                                     FieldIndex field_index,
                                     PropertyConstness constness,
                                     Representation representation) {
  Kind kind = constness == PropertyConstness::kMutable ? Kind::kField
                                                       : Kind::kConstField;
  return StoreField(isolate, kind, descriptor, field_index, representation);
}

Handle<Smi> StoreHandler::StoreSharedStructField(
    Isolate* isolate, int descriptor, FieldIndex field_index,
    Representation representation) {
  DCHECK(representation.Equals(Representation::Tagged()));
  return StoreField(isolate, Kind::kSharedStructField, descriptor, field_index,
                    representation);
}

Handle<Smi> StoreHandler::StoreNativeDataProperty(Isolate* isolate,
                                                  int descriptor) {
  int config = KindBits::encode(Kind::kNativeDataProperty) |
               DescriptorBits::encode(descriptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreAccessorFromPrototype(Isolate* isolate) {
  int config = KindBits::encode(Kind::kAccessorFromPrototype);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreApiSetter(Isolate* isolate,
                                         bool holder_is_receiver) {
  int config =
      KindBits::encode(holder_is_receiver ? Kind::kApiSetter
                                          : Kind::kApiSetterHolderIsPrototype);
  return handle(Smi::FromInt(config), isolate);
}

inline const char* WasmValueType2String(WasmValueType type) {
  switch (type) {
    case WasmValueType::kI8:
      return "i8";
    case WasmValueType::kI16:
      return "i16";
    case WasmValueType::kI32:
      return "i32";
    case WasmValueType::kU32:
      return "u32";
    case WasmValueType::kI64:
      return "i64";
    case WasmValueType::kF32:
      return "f32";
    case WasmValueType::kF64:
      return "f64";
    case WasmValueType::kS128:
      return "s128";

    case WasmValueType::kRef:
      return "Ref";
    case WasmValueType::kRefNull:
      return "RefNull";

    case WasmValueType::kNumTypes:
      return "???";
  }
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_IC_HANDLER_CONFIGURATION_INL_H_

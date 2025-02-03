// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/handler-configuration.h"

#include "src/codegen/code-factory.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/maybe-object.h"
#include "src/objects/transitions.h"

namespace v8 {
namespace internal {

namespace {

template <typename BitField>
Tagged<Smi> SetBitFieldValue(Isolate* isolate, Tagged<Smi> smi_handler,
                             typename BitField::FieldType value) {
  int config = smi_handler.value();
  config = BitField::update(config, true);
  return Smi::FromInt(config);
}

// TODO(ishell): Remove templatezation once we move common bits from
// Load/StoreHandler to the base class.
template <typename ICHandler, bool fill_handler = true>
int InitPrototypeChecksImpl(Isolate* isolate, Handle<ICHandler> handler,
                            Tagged<Smi>* smi_handler,
                            DirectHandle<Map> lookup_start_object_map,
                            MaybeObjectHandle data1,
                            MaybeObjectHandle maybe_data2) {
  int data_size = 1;
  // Holder-is-receiver case itself does not add entries unless there is an
  // optional data2 value provided.

  DCHECK_IMPLIES(IsJSGlobalObjectMap(*lookup_start_object_map),
                 lookup_start_object_map->is_prototype_map());

  if (IsPrimitiveMap(*lookup_start_object_map) ||
      lookup_start_object_map->is_access_check_needed()) {
    DCHECK(!IsJSGlobalObjectMap(*lookup_start_object_map));
    // The validity cell check for primitive and global proxy receivers does
    // not guarantee that certain native context ever had access to other
    // native context. However, a handler created for one native context could
    // be used in other native context through the megamorphic stub cache.
    // So we record the original native context to which this handler
    // corresponds.
    if (fill_handler) {
      DirectHandle<Context> native_context = isolate->native_context();
      handler->set_data2(MakeWeak(*native_context));
    } else {
      // Enable access checks on the lookup start object.
      *smi_handler = SetBitFieldValue<
          typename ICHandler::DoAccessCheckOnLookupStartObjectBits>(
          isolate, *smi_handler, true);
    }
    data_size++;
  } else if (lookup_start_object_map->is_dictionary_map() &&
             !IsJSGlobalObjectMap(*lookup_start_object_map)) {
    if (!fill_handler) {
      // Enable lookup on lookup start object.
      *smi_handler =
          SetBitFieldValue<typename ICHandler::LookupOnLookupStartObjectBits>(
              isolate, *smi_handler, true);
    }
  }
  if (fill_handler) {
    handler->set_data1(*data1);
  }
  if (!maybe_data2.is_null()) {
    if (fill_handler) {
      // This value will go either to data2 or data3 slot depending on whether
      // data2 slot is already occupied by native context.
      if (data_size == 1) {
        handler->set_data2(*maybe_data2);
      } else {
        DCHECK_EQ(2, data_size);
        handler->set_data3(*maybe_data2);
      }
    }
    data_size++;
  }
  return data_size;
}

// Returns 0 if the validity cell check is enough to ensure that the
// prototype chain from |lookup_start_object_map| till |holder| did not change.
// If the |holder| is an empty handle then the full prototype chain is
// checked.
template <typename ICHandler>
int GetHandlerDataSize(Isolate* isolate, Tagged<Smi>* smi_handler,
                       Handle<Map> lookup_start_object_map,
                       MaybeObjectHandle data1,
                       MaybeObjectHandle maybe_data2 = MaybeObjectHandle()) {
  DCHECK_NOT_NULL(smi_handler);
  return InitPrototypeChecksImpl<ICHandler, false>(
      isolate, Handle<ICHandler>(), smi_handler, lookup_start_object_map, data1,
      maybe_data2);
}

template <typename ICHandler>
void InitPrototypeChecks(Isolate* isolate, Handle<ICHandler> handler,
                         Handle<Map> lookup_start_object_map,
                         MaybeObjectHandle data1,
                         MaybeObjectHandle maybe_data2 = MaybeObjectHandle()) {
  InitPrototypeChecksImpl<ICHandler, true>(
      isolate, handler, nullptr, lookup_start_object_map, data1, maybe_data2);
}

}  // namespace

// static
Handle<Object> LoadHandler::LoadFromPrototype(
    Isolate* isolate, Handle<Map> lookup_start_object_map,
    Handle<JSReceiver> holder, Tagged<Smi> smi_handler,
    MaybeObjectHandle maybe_data1, MaybeObjectHandle maybe_data2) {
  MaybeObjectHandle data1;
  if (maybe_data1.is_null()) {
    data1 = MaybeObjectHandle::Weak(holder);
  } else {
    data1 = maybe_data1;
  }

  int data_size = GetHandlerDataSize<LoadHandler>(
      isolate, &smi_handler, lookup_start_object_map, data1, maybe_data2);

  DirectHandle<UnionOf<Smi, Cell>> validity_cell =
      Map::GetOrCreatePrototypeChainValidityCell(lookup_start_object_map,
                                                 isolate);

  Handle<LoadHandler> handler = isolate->factory()->NewLoadHandler(data_size);

  handler->set_smi_handler(smi_handler);
  handler->set_validity_cell(*validity_cell);
  InitPrototypeChecks(isolate, handler, lookup_start_object_map, data1,
                      maybe_data2);
  return handler;
}

// static
Handle<Object> LoadHandler::LoadFullChain(Isolate* isolate,
                                          Handle<Map> lookup_start_object_map,
                                          const MaybeObjectHandle& holder,
                                          Handle<Smi> smi_handler_handle) {
  Tagged<Smi> smi_handler = *smi_handler_handle;
  MaybeObjectHandle data1 = holder;
  int data_size = GetHandlerDataSize<LoadHandler>(
      isolate, &smi_handler, lookup_start_object_map, data1);

  DirectHandle<UnionOf<Smi, Cell>> validity_cell =
      Map::GetOrCreatePrototypeChainValidityCell(lookup_start_object_map,
                                                 isolate);
  if (IsSmi(*validity_cell)) {
    DCHECK_EQ(1, data_size);
    // Lookup on lookup start object isn't supported in case of a simple smi
    // handler.
    if (!LookupOnLookupStartObjectBits::decode(smi_handler.value())) {
      return smi_handler_handle;
    }
  }

  Handle<LoadHandler> handler = isolate->factory()->NewLoadHandler(data_size);

  handler->set_smi_handler(smi_handler);
  handler->set_validity_cell(*validity_cell);
  InitPrototypeChecks(isolate, handler, lookup_start_object_map, data1);
  return handler;
}

// static
KeyedAccessLoadMode LoadHandler::GetKeyedAccessLoadMode(
    Tagged<MaybeObject> handler) {
  DisallowGarbageCollection no_gc;
  if (IsSmi(handler)) {
    int const raw_handler = handler.ToSmi().value();
    Kind const kind = KindBits::decode(raw_handler);
    if (kind == Kind::kElement || kind == Kind::kIndexedString) {
      bool handle_oob = AllowOutOfBoundsBits::decode(raw_handler);
      bool handle_holes = AllowHandlingHole::decode(raw_handler);
      return CreateKeyedAccessLoadMode(handle_oob, handle_holes);
    }
  }
  return KeyedAccessLoadMode::kInBounds;
}

// static
KeyedAccessStoreMode StoreHandler::GetKeyedAccessStoreMode(
    Tagged<MaybeObject> handler) {
  DisallowGarbageCollection no_gc;
  if (IsSmi(handler)) {
    int const raw_handler = handler.ToSmi().value();
    Kind const kind = KindBits::decode(raw_handler);
    // All the handlers except the Slow Handler that use tshe
    // KeyedAccessStoreMode, compute it using KeyedAccessStoreModeForBuiltin
    // method. Hence if any other Handler get to this path, just return
    // KeyedAccessStoreMode::kInBounds.
    if (kind != Kind::kSlow) {
      return KeyedAccessStoreMode::kInBounds;
    }
    KeyedAccessStoreMode store_mode =
        KeyedAccessStoreModeBits::decode(raw_handler);
    return store_mode;
  }
  return KeyedAccessStoreMode::kInBounds;
}

// static
Handle<Object> StoreHandler::StoreElementTransition(
    Isolate* isolate, DirectHandle<Map> receiver_map,
    DirectHandle<Map> transition, KeyedAccessStoreMode store_mode,
    MaybeHandle<UnionOf<Smi, Cell>> prev_validity_cell) {
  DirectHandle<Code> code =
      ElementsTransitionAndStoreBuiltin(isolate, store_mode);
  Handle<UnionOf<Smi, Cell>> validity_cell;
  if (!prev_validity_cell.ToHandle(&validity_cell)) {
    validity_cell =
        Map::GetOrCreatePrototypeChainValidityCell(receiver_map, isolate);
  }
  Handle<StoreHandler> handler = isolate->factory()->NewStoreHandler(1);
  handler->set_smi_handler(*code);
  handler->set_validity_cell(*validity_cell);
  handler->set_data1(MakeWeak(*transition));
  return handler;
}

// static
MaybeObjectHandle StoreHandler::StoreOwnTransition(Isolate* isolate,
                                                   Handle<Map> transition_map) {
  bool is_dictionary_map = transition_map->is_dictionary_map();
#ifdef DEBUG
  if (!is_dictionary_map) {
    InternalIndex descriptor = transition_map->LastAdded();
    DirectHandle<DescriptorArray> descriptors(
        transition_map->instance_descriptors(isolate), isolate);
    PropertyDetails details = descriptors->GetDetails(descriptor);
    if (descriptors->GetKey(descriptor)->IsPrivate()) {
      DCHECK_EQ(DONT_ENUM, details.attributes());
    } else {
      DCHECK_EQ(NONE, details.attributes());
    }
    Representation representation = details.representation();
    DCHECK(!representation.IsNone());
  }
#endif
  // Declarative handlers don't support access checks.
  DCHECK(!transition_map->is_access_check_needed());

  // StoreOwnTransition does not involve any prototype checks.
  if (is_dictionary_map) {
    DCHECK(!IsJSGlobalObjectMap(*transition_map));
    int config = KindBits::encode(Kind::kNormal);
    return MaybeObjectHandle(Tagged<Object>(Smi::FromInt(config)), isolate);

  } else {
    return MaybeObjectHandle::Weak(transition_map);
  }
}

// static
MaybeObjectHandle StoreHandler::StoreTransition(Isolate* isolate,
                                                Handle<Map> transition_map) {
  bool is_dictionary_map = transition_map->is_dictionary_map();
#ifdef DEBUG
  if (!is_dictionary_map) {
    InternalIndex descriptor = transition_map->LastAdded();
    DirectHandle<DescriptorArray> descriptors(
        transition_map->instance_descriptors(isolate), isolate);
    // Private fields must be added via StoreOwnTransition handler.
    DCHECK(!descriptors->GetKey(descriptor)->IsPrivateName());
    PropertyDetails details = descriptors->GetDetails(descriptor);
    if (descriptors->GetKey(descriptor)->IsPrivate()) {
      DCHECK_EQ(DONT_ENUM, details.attributes());
    } else {
      DCHECK_EQ(NONE, details.attributes());
    }
    Representation representation = details.representation();
    DCHECK(!representation.IsNone());
  }
#endif
  // Declarative handlers don't support access checks.
  DCHECK(!transition_map->is_access_check_needed());

  // Get validity cell value if it is necessary for the handler.
  Handle<UnionOf<Smi, Cell>> validity_cell;
  if (is_dictionary_map || !transition_map->IsPrototypeValidityCellValid()) {
    validity_cell =
        Map::GetOrCreatePrototypeChainValidityCell(transition_map, isolate);
  }

  if (is_dictionary_map) {
    DCHECK(!IsJSGlobalObjectMap(*transition_map));
    Handle<StoreHandler> handler = isolate->factory()->NewStoreHandler(0);
    // Store normal with enabled lookup on receiver.
    int config = KindBits::encode(Kind::kNormal) |
                 LookupOnLookupStartObjectBits::encode(true);
    handler->set_smi_handler(Smi::FromInt(config));
    handler->set_validity_cell(*validity_cell);
    return MaybeObjectHandle(handler);

  } else {
    // Ensure the transition map contains a valid prototype validity cell.
    if (!validity_cell.is_null()) {
      transition_map->set_prototype_validity_cell(*validity_cell,
                                                  kRelaxedStore);
    }
    return MaybeObjectHandle::Weak(transition_map);
  }
}

// static
Handle<Object> StoreHandler::StoreThroughPrototype(
    Isolate* isolate, Handle<Map> receiver_map, Handle<JSReceiver> holder,
    Tagged<Smi> smi_handler, MaybeObjectHandle maybe_data1,
    MaybeObjectHandle maybe_data2) {
  MaybeObjectHandle data1;
  if (maybe_data1.is_null()) {
    data1 = MaybeObjectHandle::Weak(holder);
  } else {
    data1 = maybe_data1;
  }

  int data_size = GetHandlerDataSize<StoreHandler>(
      isolate, &smi_handler, receiver_map, data1, maybe_data2);

  DirectHandle<UnionOf<Smi, Cell>> validity_cell =
      Map::GetOrCreatePrototypeChainValidityCell(receiver_map, isolate);

  Handle<StoreHandler> handler = isolate->factory()->NewStoreHandler(data_size);

  handler->set_smi_handler(smi_handler);
  handler->set_validity_cell(*validity_cell);
  InitPrototypeChecks(isolate, handler, receiver_map, data1, maybe_data2);
  return handler;
}

// static
MaybeObjectHandle StoreHandler::StoreGlobal(Handle<PropertyCell> cell) {
  return MaybeObjectHandle::Weak(cell);
}

// static
Handle<Object> StoreHandler::StoreProxy(Isolate* isolate,
                                        Handle<Map> receiver_map,
                                        Handle<JSProxy> proxy,
                                        Handle<JSReceiver> receiver) {
  Handle<Smi> smi_handler = StoreProxy(isolate);
  if (receiver.is_identical_to(proxy)) return smi_handler;
  return StoreThroughPrototype(isolate, receiver_map, proxy, *smi_handler,
                               MaybeObjectHandle::Weak(proxy));
}

bool LoadHandler::CanHandleHolderNotLookupStart(Tagged<Object> handler) {
  if (IsSmi(handler)) {
    auto kind = LoadHandler::KindBits::decode(handler.ToSmi().value());
    return kind == LoadHandler::Kind::kSlow ||
           kind == LoadHandler::Kind::kNonExistent;
  }
  return IsLoadHandler(handler);
}

#if defined(OBJECT_PRINT)
namespace {
void PrintSmiLoadHandler(int raw_handler, std::ostream& os) {
  LoadHandler::Kind kind = LoadHandler::KindBits::decode(raw_handler);
  os << "kind = ";
  switch (kind) {
    case LoadHandler::Kind::kElement:
      os << "kElement, ";
      if (LoadHandler::IsWasmArrayBits::decode(raw_handler)) {
        os << "WasmArray, "
           << LoadHandler::WasmArrayTypeBits::decode(raw_handler);

      } else {
        os << "allow out of bounds = "
           << LoadHandler::AllowOutOfBoundsBits::decode(raw_handler)
           << ", is JSArray = "
           << LoadHandler::IsJsArrayBits::decode(raw_handler)
           << ", alow reading holes = "
           << LoadHandler::AllowHandlingHole::decode(raw_handler)
           << ", elements kind = "
           << ElementsKindToString(
                  LoadHandler::ElementsKindBits::decode(raw_handler));
      }
      break;
    case LoadHandler::Kind::kIndexedString:
      os << "kIndexedString, allow out of bounds = "
         << LoadHandler::AllowOutOfBoundsBits::decode(raw_handler);
      break;
    case LoadHandler::Kind::kNormal:
      os << "kNormal";
      break;
    case LoadHandler::Kind::kGlobal:
      os << "kGlobal";
      break;
    case LoadHandler::Kind::kField: {
      if (LoadHandler::IsWasmStructBits::decode(raw_handler)) {
        os << "kField, WasmStruct, type = "
           << LoadHandler::WasmFieldTypeBits::decode(raw_handler)
           << ", field offset = "
           << LoadHandler::WasmFieldOffsetBits::decode(raw_handler);
      } else {
        os << "kField, is in object = "
           << LoadHandler::IsInobjectBits::decode(raw_handler)
           << ", is double = " << LoadHandler::IsDoubleBits::decode(raw_handler)
           << ", field index = "
           << LoadHandler::FieldIndexBits::decode(raw_handler);
      }
      break;
    }
    case LoadHandler::Kind::kConstantFromPrototype:
      os << "kConstantFromPrototype";
      break;
    case LoadHandler::Kind::kAccessorFromPrototype:
      os << "kAccessorFromPrototype";
      break;
    case LoadHandler::Kind::kNativeDataProperty:
      os << "kNativeDataProperty, descriptor = "
         << LoadHandler::DescriptorBits::decode(raw_handler);
      break;
    case LoadHandler::Kind::kApiGetter:
      os << "kApiGetter";
      break;
    case LoadHandler::Kind::kApiGetterHolderIsPrototype:
      os << "kApiGetterHolderIsPrototype";
      break;
    case LoadHandler::Kind::kInterceptor:
      os << "kInterceptor";
      break;
    case LoadHandler::Kind::kSlow:
      os << "kSlow";
      break;
    case LoadHandler::Kind::kProxy:
      os << "kProxy";
      break;
    case LoadHandler::Kind::kNonExistent:
      os << "kNonExistent";
      break;
    case LoadHandler::Kind::kModuleExport:
      os << "kModuleExport, exports index = "
         << LoadHandler::ExportsIndexBits::decode(raw_handler);
      break;
    default:
      os << "<invalid value " << static_cast<int>(kind) << ">";
      break;
  }
}

void PrintSmiStoreHandler(int raw_handler, std::ostream& os) {
  StoreHandler::Kind kind = StoreHandler::KindBits::decode(raw_handler);
  os << "kind = ";
  switch (kind) {
    case StoreHandler::Kind::kField:
    case StoreHandler::Kind::kConstField: {
      os << "k";
      if (kind == StoreHandler::Kind::kConstField) {
        os << "Const";
      }
      Representation representation = Representation::FromKind(
          StoreHandler::RepresentationBits::decode(raw_handler));
      os << "Field, descriptor = "
         << StoreHandler::DescriptorBits::decode(raw_handler)
         << ", is in object = "
         << StoreHandler::IsInobjectBits::decode(raw_handler)
         << ", representation = " << representation.Mnemonic()
         << ", field index = "
         << StoreHandler::FieldIndexBits::decode(raw_handler);
      break;
    }
    case StoreHandler::Kind::kAccessorFromPrototype:
      os << "kAccessorFromPrototype";
      break;
    case StoreHandler::Kind::kNativeDataProperty:
      os << "kNativeDataProperty, descriptor = "
         << StoreHandler::DescriptorBits::decode(raw_handler);
      break;
    case StoreHandler::Kind::kApiSetter:
      os << "kApiSetter";
      break;
    case StoreHandler::Kind::kApiSetterHolderIsPrototype:
      os << "kApiSetterHolderIsPrototype";
      break;
    case StoreHandler::Kind::kGlobalProxy:
      os << "kGlobalProxy";
      break;
    case StoreHandler::Kind::kNormal:
      os << "kNormal";
      break;
    case StoreHandler::Kind::kInterceptor:
      os << "kInterceptor";
      break;
    case StoreHandler::Kind::kSlow: {
      KeyedAccessStoreMode keyed_access_store_mode =
          StoreHandler::KeyedAccessStoreModeBits::decode(raw_handler);
      os << "kSlow, keyed access store mode = " << keyed_access_store_mode;
      break;
    }
    case StoreHandler::Kind::kProxy:
      os << "kProxy";
      break;
    case StoreHandler::Kind::kSharedStructField:
      os << "kSharedStructField";
      break;
    case StoreHandler::Kind::kKindsNumber:
      UNREACHABLE();
  }
}

}  // namespace

// static
void LoadHandler::PrintHandler(Tagged<Object> handler, std::ostream& os) {
  DisallowGarbageCollection no_gc;
  if (IsSmi(handler)) {
    int raw_handler = handler.ToSmi().value();
    os << "LoadHandler(Smi)(";
    PrintSmiLoadHandler(raw_handler, os);
    os << ")";
  } else if (IsCode(handler)) {
    os << "LoadHandler(Code)("
       << Builtins::name(Cast<Code>(handler)->builtin_id()) << ")";
  } else if (IsSymbol(handler)) {
    os << "LoadHandler(Symbol)(" << Brief(Cast<Symbol>(handler)) << ")";
  } else if (IsLoadHandler(handler)) {
    Tagged<LoadHandler> load_handler = Cast<LoadHandler>(handler);
    int raw_handler = Cast<Smi>(load_handler->smi_handler()).value();
    os << "LoadHandler(do access check on lookup start object = "
       << DoAccessCheckOnLookupStartObjectBits::decode(raw_handler)
       << ", lookup on lookup start object = "
       << LookupOnLookupStartObjectBits::decode(raw_handler) << ", ";
    PrintSmiLoadHandler(raw_handler, os);
    if (load_handler->data_field_count() >= 1) {
      os << ", data1 = ";
      ShortPrint(load_handler->data1(), os);
    }
    if (load_handler->data_field_count() >= 2) {
      os << ", data2 = ";
      ShortPrint(load_handler->data2(), os);
    }
    if (load_handler->data_field_count() >= 3) {
      os << ", data3 = ";
      ShortPrint(load_handler->data3(), os);
    }
    os << ", validity cell = ";
    ShortPrint(load_handler->validity_cell(), os);
    os << ")";
  } else {
    os << "LoadHandler(<unexpected>)(" << Brief(handler) << ")";
  }
}

void StoreHandler::PrintHandler(Tagged<Object> handler, std::ostream& os) {
  DisallowGarbageCollection no_gc;
  if (IsSmi(handler)) {
    int raw_handler = handler.ToSmi().value();
    os << "StoreHandler(Smi)(";
    PrintSmiStoreHandler(raw_handler, os);
    os << ")" << std::endl;
  } else if (IsStoreHandler(handler)) {
    os << "StoreHandler(";
    Tagged<StoreHandler> store_handler = Cast<StoreHandler>(handler);
    if (IsCode(store_handler->smi_handler())) {
      Tagged<Code> code = Cast<Code>(store_handler->smi_handler());
      os << "builtin = ";
      ShortPrint(code, os);
    } else {
      int raw_handler = Cast<Smi>(store_handler->smi_handler()).value();
      os << "do access check on lookup start object = "
         << DoAccessCheckOnLookupStartObjectBits::decode(raw_handler)
         << ", lookup on lookup start object = "
         << LookupOnLookupStartObjectBits::decode(raw_handler) << ", ";
      PrintSmiStoreHandler(raw_handler, os);
    }
    if (store_handler->data_field_count() >= 1) {
      os << ", data1 = ";
      ShortPrint(store_handler->data1(), os);
    }
    if (store_handler->data_field_count() >= 2) {
      os << ", data2 = ";
      ShortPrint(store_handler->data2(), os);
    }
    if (store_handler->data_field_count() >= 3) {
      os << ", data3 = ";
      ShortPrint(store_handler->data3(), os);
    }
    os << ", validity cell = ";
    ShortPrint(store_handler->validity_cell(), os);
    os << ")" << std::endl;
  } else if (IsMap(handler)) {
    os << "StoreHandler(field transition to " << Brief(handler) << ")"
       << std::endl;
  } else if (IsCode(handler)) {
    Tagged<Code> code = Cast<Code>(handler);
    os << "StoreHandler(builtin = ";
    ShortPrint(code, os);
    os << ")" << std::endl;
  } else {
    os << "StoreHandler(<unexpected>)(" << Brief(handler) << ")" << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, WasmValueType type) {
  return os << WasmValueType2String(type);
}

#endif  // defined(OBJECT_PRINT)

}  // namespace internal
}  // namespace v8

// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-wasm-objects.h"

#include <optional>

#include "src/api/api-inl.h"
#include "src/api/api-natives.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/debug/debug-wasm-objects-inl.h"
#include "src/execution/frames-inl.h"
#include "src/objects/allocation-site.h"
#include "src/objects/property-descriptor.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-value.h"

namespace v8 {
namespace internal {
namespace {

using StringBuilder = wasm::StringBuilder;
Handle<String> ToInternalString(StringBuilder& sb, Isolate* isolate) {
  return isolate->factory()->InternalizeString(
      base::VectorOf(sb.start(), sb.length()));
}

enum DebugProxyId {
  kFunctionsProxy,
  kGlobalsProxy,
  kMemoriesProxy,
  kTablesProxy,
  kLastInstanceProxyId = kTablesProxy,

  kContextProxy,
  kLocalsProxy,
  kStackProxy,
  kStructProxy,
  kArrayProxy,
  kLastProxyId = kArrayProxy,

  kNumProxies = kLastProxyId + 1,
  kNumInstanceProxies = kLastInstanceProxyId + 1
};

constexpr int kWasmValueMapIndex = kNumProxies;
constexpr int kNumDebugMaps = kWasmValueMapIndex + 1;

Handle<FixedArray> GetOrCreateDebugMaps(Isolate* isolate) {
  Handle<FixedArray> maps = isolate->wasm_debug_maps();
  if (maps->length() == 0) {
    maps = isolate->factory()->NewFixedArrayWithHoles(kNumDebugMaps);
    isolate->native_context()->set_wasm_debug_maps(*maps);
  }
  return maps;
}

// Creates a Map for the given debug proxy |id| using the |create_template_fn|
// on-demand and caches this map in the global object. The map is derived from
// the FunctionTemplate returned by |create_template_fn| and has its prototype
// set to |null| and is marked non-extensible (by default).
// TODO(bmeurer): remove the extensibility opt-out and replace it with a proper
// way to add non-intercepted named properties.
Handle<Map> GetOrCreateDebugProxyMap(
    Isolate* isolate, DebugProxyId id,
    v8::Local<v8::FunctionTemplate> (*create_template_fn)(v8::Isolate*),
    bool make_non_extensible = true) {
  auto maps = GetOrCreateDebugMaps(isolate);
  CHECK_LE(kNumProxies, maps->length());
  if (!maps->is_the_hole(isolate, id)) {
    return handle(Cast<Map>(maps->get(id)), isolate);
  }
  auto tmp = (*create_template_fn)(reinterpret_cast<v8::Isolate*>(isolate));
  auto fun = ApiNatives::InstantiateFunction(isolate, Utils::OpenHandle(*tmp))
                 .ToHandleChecked();
  auto map = JSFunction::GetDerivedMap(isolate, fun, fun).ToHandleChecked();
  Map::SetPrototype(isolate, map, isolate->factory()->null_value());
  if (make_non_extensible) {
    map->set_is_extensible(false);
  }
  maps->set(id, *map);
  return map;
}

// Base class for debug proxies, offers indexed access. The subclasses
// need to implement |Count| and |Get| methods appropriately.
template <typename T, DebugProxyId id, typename Provider>
struct IndexedDebugProxy {
  static constexpr DebugProxyId kId = id;

  static Handle<JSObject> Create(Isolate* isolate, Handle<Provider> provider,
                                 bool make_map_non_extensible = true) {
    auto object_map = GetOrCreateDebugProxyMap(isolate, kId, &T::CreateTemplate,
                                               make_map_non_extensible);
    auto object = isolate->factory()->NewFastOrSlowJSObjectFromMap(
        object_map, 0, AllocationType::kYoung,
        DirectHandle<AllocationSite>::null(), NewJSObjectType::kAPIWrapper);
    object->SetEmbedderField(kProviderField, *provider);
    return object;
  }

  enum {
    kProviderField,
    kFieldCount,
  };

  static v8::Local<v8::FunctionTemplate> CreateTemplate(v8::Isolate* isolate) {
    Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
    templ->SetClassName(
        v8::String::NewFromUtf8(isolate, T::kClassName).ToLocalChecked());
    templ->InstanceTemplate()->SetInternalFieldCount(T::kFieldCount);
    templ->InstanceTemplate()->SetHandler(
        v8::IndexedPropertyHandlerConfiguration(
            &T::IndexedGetter, {}, &T::IndexedQuery, {}, &T::IndexedEnumerator,
            {}, &T::IndexedDescriptor, {},
            v8::PropertyHandlerFlags::kHasNoSideEffect));
    return templ;
  }

  template <typename V>
  static Isolate* GetIsolate(const PropertyCallbackInfo<V>& info) {
    return reinterpret_cast<Isolate*>(info.GetIsolate());
  }

  template <typename V>
  static Handle<JSObject> GetHolder(const PropertyCallbackInfo<V>& info) {
    return Cast<JSObject>(Utils::OpenHandle(*info.HolderV2()));
  }

  static Handle<Provider> GetProvider(DirectHandle<JSObject> holder,
                                      Isolate* isolate) {
    return handle(Cast<Provider>(holder->GetEmbedderField(kProviderField)),
                  isolate);
  }

  template <typename V>
  static Handle<Provider> GetProvider(const PropertyCallbackInfo<V>& info) {
    return GetProvider(GetHolder(info), GetIsolate(info));
  }

  static v8::Intercepted IndexedGetter(
      uint32_t index, const PropertyCallbackInfo<v8::Value>& info) {
    auto isolate = GetIsolate(info);
    auto provider = GetProvider(info);
    if (index < T::Count(isolate, provider)) {
      auto value = T::Get(isolate, provider, index);
      info.GetReturnValue().Set(Utils::ToLocal(value));
      return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted IndexedDescriptor(
      uint32_t index, const PropertyCallbackInfo<v8::Value>& info) {
    auto isolate = GetIsolate(info);
    auto provider = GetProvider(info);
    if (index < T::Count(isolate, provider)) {
      PropertyDescriptor descriptor;
      descriptor.set_configurable(false);
      descriptor.set_enumerable(true);
      descriptor.set_writable(false);
      descriptor.set_value(Cast<JSAny>(T::Get(isolate, provider, index)));
      info.GetReturnValue().Set(Utils::ToLocal(descriptor.ToObject(isolate)));
      return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted IndexedQuery(
      uint32_t index, const PropertyCallbackInfo<v8::Integer>& info) {
    if (index < T::Count(GetIsolate(info), GetProvider(info))) {
      info.GetReturnValue().Set(Integer::New(
          info.GetIsolate(),
          PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly));
      return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
  }

  static void IndexedEnumerator(const PropertyCallbackInfo<v8::Array>& info) {
    auto isolate = GetIsolate(info);
    auto count = T::Count(isolate, GetProvider(info));
    auto indices = isolate->factory()->NewFixedArray(count);
    for (uint32_t index = 0; index < count; ++index) {
      indices->set(index, Smi::FromInt(index));
    }
    info.GetReturnValue().Set(
        Utils::ToLocal(isolate->factory()->NewJSArrayWithElements(
            indices, PACKED_SMI_ELEMENTS)));
  }
};

// Extends |IndexedDebugProxy| with named access, where the names are computed
// on-demand, and all names are assumed to start with a dollar char ($). This
// is important in order to scale to Wasm modules with hundreds of thousands
// of functions in them.
template <typename T, DebugProxyId id, typename Provider = WasmInstanceObject>
struct NamedDebugProxy : IndexedDebugProxy<T, id, Provider> {
  static v8::Local<v8::FunctionTemplate> CreateTemplate(v8::Isolate* isolate) {
    auto templ = IndexedDebugProxy<T, id, Provider>::CreateTemplate(isolate);
    templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
        &T::NamedGetter, {}, &T::NamedQuery, {}, &T::NamedEnumerator, {},
        &T::NamedDescriptor, {}, v8::PropertyHandlerFlags::kHasNoSideEffect));
    return templ;
  }

  static void IndexedEnumerator(const PropertyCallbackInfo<v8::Array>& info) {
    info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
  }

  static Handle<NameDictionary> GetNameTable(Handle<JSObject> holder,
                                             Isolate* isolate) {
    Handle<Symbol> symbol = isolate->factory()->wasm_debug_proxy_names_symbol();
    Handle<Object> table_or_undefined =
        JSObject::GetProperty(isolate, holder, symbol).ToHandleChecked();
    if (!IsUndefined(*table_or_undefined, isolate)) {
      return Cast<NameDictionary>(table_or_undefined);
    }
    auto provider = T::GetProvider(holder, isolate);
    auto count = T::Count(isolate, provider);
    auto table = NameDictionary::New(isolate, count);
    for (uint32_t index = 0; index < count; ++index) {
      HandleScope scope(isolate);
      auto key = T::GetName(isolate, provider, index);
      if (table->FindEntry(isolate, key).is_found()) continue;
      Handle<Smi> value(Smi::FromInt(index), isolate);
      table = NameDictionary::Add(isolate, table, key, value,
                                  PropertyDetails::Empty());
    }
    Object::SetProperty(isolate, holder, symbol, table).Check();
    return table;
  }

  template <typename V>
  static std::optional<uint32_t> FindName(Local<v8::Name> name,
                                          const PropertyCallbackInfo<V>& info) {
    if (!name->IsString()) return {};
    auto name_str = Utils::OpenHandle(*name.As<v8::String>());
    if (name_str->length() == 0 || name_str->Get(0) != '$') return {};
    auto isolate = T::GetIsolate(info);
    auto table = GetNameTable(T::GetHolder(info), isolate);
    auto entry = table->FindEntry(isolate, name_str);
    if (entry.is_found()) return Smi::ToInt(table->ValueAt(entry));
    return {};
  }

  static v8::Intercepted NamedGetter(
      Local<v8::Name> name, const PropertyCallbackInfo<v8::Value>& info) {
    if (auto index = FindName(name, info)) {
      return T::IndexedGetter(*index, info);
    }
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted NamedQuery(
      Local<v8::Name> name, const PropertyCallbackInfo<v8::Integer>& info) {
    if (auto index = FindName(name, info)) {
      return T::IndexedQuery(*index, info);
    }
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted NamedDescriptor(
      Local<v8::Name> name, const PropertyCallbackInfo<v8::Value>& info) {
    if (auto index = FindName(name, info)) {
      return T::IndexedDescriptor(*index, info);
    }
    return v8::Intercepted::kNo;
  }

  static void NamedEnumerator(const PropertyCallbackInfo<v8::Array>& info) {
    auto isolate = T::GetIsolate(info);
    auto table = GetNameTable(T::GetHolder(info), isolate);
    auto names = NameDictionary::IterationIndices(isolate, table);
    for (int i = 0; i < names->length(); ++i) {
      InternalIndex entry(Smi::ToInt(names->get(i)));
      names->set(i, table->NameAt(entry));
    }
    info.GetReturnValue().Set(Utils::ToLocal(
        isolate->factory()->NewJSArrayWithElements(names, PACKED_ELEMENTS)));
  }
};

// This class implements the "functions" proxy.
struct FunctionsProxy : NamedDebugProxy<FunctionsProxy, kFunctionsProxy> {
  static constexpr char const* kClassName = "Functions";

  static uint32_t Count(Isolate* isolate,
                        DirectHandle<WasmInstanceObject> instance) {
    return static_cast<uint32_t>(instance->module()->functions.size());
  }

  static Handle<Object> Get(Isolate* isolate,
                            DirectHandle<WasmInstanceObject> instance,
                            uint32_t index) {
    DirectHandle<WasmTrustedInstanceData> trusted_data{
        instance->trusted_data(isolate), isolate};
    DirectHandle<WasmFuncRef> func_ref =
        WasmTrustedInstanceData::GetOrCreateFuncRef(isolate, trusted_data,
                                                    index);
    DirectHandle<WasmInternalFunction> internal_function{
        func_ref->internal(isolate), isolate};
    return WasmInternalFunction::GetOrCreateExternal(internal_function);
  }

  static Handle<String> GetName(Isolate* isolate,
                                DirectHandle<WasmInstanceObject> instance,
                                uint32_t index) {
    i::DirectHandle<i::WasmTrustedInstanceData> instance_data{
        instance->trusted_data(isolate), isolate};
    return GetWasmFunctionDebugName(isolate, instance_data, index);
  }
};

// This class implements the "globals" proxy.
struct GlobalsProxy : NamedDebugProxy<GlobalsProxy, kGlobalsProxy> {
  static constexpr char const* kClassName = "Globals";

  static uint32_t Count(Isolate* isolate,
                        DirectHandle<WasmInstanceObject> instance) {
    return static_cast<uint32_t>(instance->module()->globals.size());
  }

  static Handle<Object> Get(Isolate* isolate,
                            DirectHandle<WasmInstanceObject> instance,
                            uint32_t index) {
    Handle<WasmModuleObject> module(instance->module_object(), isolate);
    return WasmValueObject::New(
        isolate,
        instance->trusted_data(isolate)->GetGlobalValue(
            isolate, instance->module()->globals[index]),
        module);
  }

  static Handle<String> GetName(Isolate* isolate,
                                DirectHandle<WasmInstanceObject> instance,
                                uint32_t index) {
    wasm::NamesProvider* names =
        instance->module_object()->native_module()->GetNamesProvider();
    StringBuilder sb;
    names->PrintGlobalName(sb, index);
    return ToInternalString(sb, isolate);
  }
};

// This class implements the "memories" proxy.
struct MemoriesProxy : NamedDebugProxy<MemoriesProxy, kMemoriesProxy> {
  static constexpr char const* kClassName = "Memories";

  static uint32_t Count(Isolate* isolate,
                        DirectHandle<WasmInstanceObject> instance) {
    return instance->trusted_data(isolate)->memory_objects()->length();
  }

  static Handle<Object> Get(Isolate* isolate,
                            DirectHandle<WasmInstanceObject> instance,
                            uint32_t index) {
    return handle(instance->trusted_data(isolate)->memory_object(index),
                  isolate);
  }

  static Handle<String> GetName(Isolate* isolate,
                                DirectHandle<WasmInstanceObject> instance,
                                uint32_t index) {
    wasm::NamesProvider* names =
        instance->module_object()->native_module()->GetNamesProvider();
    StringBuilder sb;
    names->PrintMemoryName(sb, index);
    return ToInternalString(sb, isolate);
  }
};

// This class implements the "tables" proxy.
struct TablesProxy : NamedDebugProxy<TablesProxy, kTablesProxy> {
  static constexpr char const* kClassName = "Tables";

  static uint32_t Count(Isolate* isolate,
                        DirectHandle<WasmInstanceObject> instance) {
    return instance->trusted_data(isolate)->tables()->length();
  }

  static Handle<Object> Get(Isolate* isolate,
                            DirectHandle<WasmInstanceObject> instance,
                            uint32_t index) {
    return handle(instance->trusted_data(isolate)->tables()->get(index),
                  isolate);
  }

  static Handle<String> GetName(Isolate* isolate,
                                DirectHandle<WasmInstanceObject> instance,
                                uint32_t index) {
    wasm::NamesProvider* names =
        instance->module_object()->native_module()->GetNamesProvider();
    StringBuilder sb;
    names->PrintTableName(sb, index);
    return ToInternalString(sb, isolate);
  }
};

// This class implements the "locals" proxy.
struct LocalsProxy : NamedDebugProxy<LocalsProxy, kLocalsProxy, FixedArray> {
  static constexpr char const* kClassName = "Locals";

  static Handle<JSObject> Create(WasmFrame* frame) {
    auto isolate = frame->isolate();
    auto debug_info = frame->native_module()->GetDebugInfo();
    // TODO(bmeurer): Check if pc is inspectable.
    int count = debug_info->GetNumLocals(frame->pc(), isolate);
    auto function = debug_info->GetFunctionAtAddress(frame->pc(), isolate);
    auto values = isolate->factory()->NewFixedArray(count + 2);
    Handle<WasmModuleObject> module_object(
        frame->wasm_instance()->module_object(), isolate);
    for (int i = 0; i < count; ++i) {
      auto value = WasmValueObject::New(
          isolate,
          debug_info->GetLocalValue(i, frame->pc(), frame->fp(),
                                    frame->callee_fp(), isolate),
          module_object);
      values->set(i, *value);
    }
    values->set(count + 0, frame->wasm_instance()->module_object());
    values->set(count + 1, Smi::FromInt(function.func_index));
    return NamedDebugProxy::Create(isolate, values);
  }

  static uint32_t Count(Isolate* isolate, DirectHandle<FixedArray> values) {
    return values->length() - 2;
  }

  static Handle<Object> Get(Isolate* isolate, DirectHandle<FixedArray> values,
                            uint32_t index) {
    return handle(values->get(index), isolate);
  }

  static Handle<String> GetName(Isolate* isolate,
                                DirectHandle<FixedArray> values,
                                uint32_t index) {
    uint32_t count = Count(isolate, values);
    auto native_module =
        Cast<WasmModuleObject>(values->get(count + 0))->native_module();
    auto function_index = Smi::ToInt(Cast<Smi>(values->get(count + 1)));
    wasm::NamesProvider* names = native_module->GetNamesProvider();
    StringBuilder sb;
    names->PrintLocalName(sb, function_index, index);
    return ToInternalString(sb, isolate);
  }
};

// This class implements the "stack" proxy (which offers only indexed access).
struct StackProxy : IndexedDebugProxy<StackProxy, kStackProxy, FixedArray> {
  static constexpr char const* kClassName = "Stack";

  static Handle<JSObject> Create(WasmFrame* frame) {
    auto isolate = frame->isolate();
    auto debug_info =
        frame->trusted_instance_data()->native_module()->GetDebugInfo();
    int count = debug_info->GetStackDepth(frame->pc(), isolate);
    auto values = isolate->factory()->NewFixedArray(count);
    Handle<WasmModuleObject> module_object(
        frame->wasm_instance()->module_object(), isolate);
    for (int i = 0; i < count; ++i) {
      auto value = WasmValueObject::New(
          isolate,
          debug_info->GetStackValue(i, frame->pc(), frame->fp(),
                                    frame->callee_fp(), isolate),
          module_object);
      values->set(i, *value);
    }
    return IndexedDebugProxy::Create(isolate, values);
  }

  static uint32_t Count(Isolate* isolate, DirectHandle<FixedArray> values) {
    return values->length();
  }

  static Handle<Object> Get(Isolate* isolate, DirectHandle<FixedArray> values,
                            uint32_t index) {
    return handle(values->get(index), isolate);
  }
};

// Creates FixedArray with size |kNumInstanceProxies| as cache on-demand
// on the |instance|, stored under the |wasm_debug_proxy_cache_symbol|.
// This is used to cache the various instance debug proxies (functions,
// globals, tables, and memories) on the WasmInstanceObject.
Handle<FixedArray> GetOrCreateInstanceProxyCache(
    Isolate* isolate, Handle<WasmInstanceObject> instance) {
  Handle<Object> cache;
  Handle<Symbol> symbol = isolate->factory()->wasm_debug_proxy_cache_symbol();
  if (!Object::GetProperty(isolate, instance, symbol).ToHandle(&cache) ||
      IsUndefined(*cache, isolate)) {
    cache = isolate->factory()->NewFixedArrayWithHoles(kNumInstanceProxies);
    Object::SetProperty(isolate, instance, symbol, cache).Check();
  }
  return Cast<FixedArray>(cache);
}

// Creates an instance of the |Proxy| on-demand and caches that on the
// |instance|.
template <typename Proxy>
Handle<JSObject> GetOrCreateInstanceProxy(Isolate* isolate,
                                          Handle<WasmInstanceObject> instance) {
  static_assert(Proxy::kId < kNumInstanceProxies);
  DirectHandle<FixedArray> proxies =
      GetOrCreateInstanceProxyCache(isolate, instance);
  if (!proxies->is_the_hole(isolate, Proxy::kId)) {
    return handle(Cast<JSObject>(proxies->get(Proxy::kId)), isolate);
  }
  Handle<JSObject> proxy = Proxy::Create(isolate, instance);
  proxies->set(Proxy::kId, *proxy);
  return proxy;
}

// This class implements the debug proxy for a given Wasm frame. The debug
// proxy is used when evaluating JavaScript expressions on a wasm frame via
// the inspector |Runtime.evaluateOnCallFrame()| API and enables developers
// and extensions to inspect the WebAssembly engine state from JavaScript.
// The proxy provides the following interface:
//
// type WasmValue = {
//   type: string;
//   value: number | bigint | object | string;
// };
// type WasmFunction = (... args : WasmValue[]) = > WasmValue;
// interface WasmInterface {
//   $globalX: WasmValue;
//   $varX: WasmValue;
//   $funcX(a : WasmValue /*, ...*/) : WasmValue;
//   readonly $memoryX : WebAssembly.Memory;
//   readonly $tableX : WebAssembly.Table;
//
//   readonly instance : WebAssembly.Instance;
//   readonly module : WebAssembly.Module;
//
//   readonly memories : {[nameOrIndex:string | number] : WebAssembly.Memory};
//   readonly tables : {[nameOrIndex:string | number] : WebAssembly.Table};
//   readonly stack : WasmValue[];
//   readonly globals : {[nameOrIndex:string | number] : WasmValue};
//   readonly locals : {[nameOrIndex:string | number] : WasmValue};
//   readonly functions : {[nameOrIndex:string | number] : WasmFunction};
// }
//
// The wasm index spaces memories, tables, stack, globals, locals, and
// functions are JSObjects with interceptors that lazily produce values
// either by index or by name (except for stack).
// Only the names are reported by APIs such as Object.keys() and
// Object.getOwnPropertyNames(), since the indices are not meant to be
// used interactively by developers (in Chrome DevTools), but are provided
// for WebAssembly language extensions. Also note that these JSObjects
// all have null prototypes, to not confuse context lookup and to make
// their purpose as dictionaries clear.
//
// See http://doc/1VZOJrU2VsqOZe3IUzbwQWQQSZwgGySsm5119Ust1gUA and
// http://bit.ly/devtools-wasm-entities for more details.
class ContextProxyPrototype {
 public:
  static Handle<JSObject> Create(Isolate* isolate) {
    auto object_map =
        GetOrCreateDebugProxyMap(isolate, kContextProxy, &CreateTemplate);
    return isolate->factory()->NewJSObjectFromMap(
        object_map, AllocationType::kYoung,
        DirectHandle<AllocationSite>::null(), NewJSObjectType::kAPIWrapper);
  }

 private:
  static v8::Local<v8::FunctionTemplate> CreateTemplate(v8::Isolate* isolate) {
    Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
    templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
        &NamedGetter, {}, {}, {}, {}, {}, {}, {},
        static_cast<v8::PropertyHandlerFlags>(
            static_cast<unsigned>(
                v8::PropertyHandlerFlags::kOnlyInterceptStrings) |
            static_cast<unsigned>(
                v8::PropertyHandlerFlags::kHasNoSideEffect))));
    return templ;
  }

  static MaybeHandle<Object> GetNamedProperty(Isolate* isolate,
                                              Handle<JSObject> receiver,
                                              Handle<String> name) {
    if (name->length() != 0 && name->Get(0) == '$') {
      const char* kDelegateNames[] = {"memories", "locals", "tables",
                                      "functions", "globals"};
      for (auto delegate_name : kDelegateNames) {
        Handle<Object> delegate;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, delegate,
            JSObject::GetProperty(isolate, receiver, delegate_name));
        if (!IsUndefined(*delegate, isolate)) {
          Handle<Object> value;
          ASSIGN_RETURN_ON_EXCEPTION(
              isolate, value, Object::GetProperty(isolate, delegate, name));
          if (!IsUndefined(*value, isolate)) return value;
        }
      }
    }
    return {};
  }

  static v8::Intercepted NamedGetter(
      Local<v8::Name> name, const PropertyCallbackInfo<v8::Value>& info) {
    auto name_string = Cast<String>(Utils::OpenHandle(*name));
    auto isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
    auto receiver = Cast<JSObject>(Utils::OpenHandle(*info.This()));
    Handle<Object> value;
    if (GetNamedProperty(isolate, receiver, name_string).ToHandle(&value)) {
      info.GetReturnValue().Set(Utils::ToLocal(value));
      return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
  }
};

class ContextProxy {
 public:
  static Handle<JSObject> Create(WasmFrame* frame) {
    Isolate* isolate = frame->isolate();
    auto object = isolate->factory()->NewSlowJSObjectWithNullProto();
    Handle<WasmInstanceObject> instance(frame->wasm_instance(), isolate);
    JSObject::AddProperty(isolate, object, "instance", instance, FROZEN);
    DirectHandle<WasmModuleObject> module_object(instance->module_object(),
                                                 isolate);
    JSObject::AddProperty(isolate, object, "module", module_object, FROZEN);
    auto locals = LocalsProxy::Create(frame);
    JSObject::AddProperty(isolate, object, "locals", locals, FROZEN);
    auto stack = StackProxy::Create(frame);
    JSObject::AddProperty(isolate, object, "stack", stack, FROZEN);
    auto memories = GetOrCreateInstanceProxy<MemoriesProxy>(isolate, instance);
    JSObject::AddProperty(isolate, object, "memories", memories, FROZEN);
    auto tables = GetOrCreateInstanceProxy<TablesProxy>(isolate, instance);
    JSObject::AddProperty(isolate, object, "tables", tables, FROZEN);
    auto globals = GetOrCreateInstanceProxy<GlobalsProxy>(isolate, instance);
    JSObject::AddProperty(isolate, object, "globals", globals, FROZEN);
    auto functions =
        GetOrCreateInstanceProxy<FunctionsProxy>(isolate, instance);
    JSObject::AddProperty(isolate, object, "functions", functions, FROZEN);
    Handle<JSObject> prototype = ContextProxyPrototype::Create(isolate);
    JSObject::SetPrototype(isolate, object, prototype, false, kDontThrow)
        .Check();
    return object;
  }
};

class DebugWasmScopeIterator final : public debug::ScopeIterator {
 public:
  explicit DebugWasmScopeIterator(WasmFrame* frame)
      : frame_(frame),
        type_(debug::ScopeIterator::ScopeTypeWasmExpressionStack) {
    // Skip local scope and expression stack scope if the frame is not
    // inspectable.
    if (!frame->is_inspectable()) {
      type_ = debug::ScopeIterator::ScopeTypeModule;
    }
  }

  bool Done() override { return type_ == ScopeTypeWith; }

  void Advance() override {
    DCHECK(!Done());
    switch (type_) {
      case ScopeTypeWasmExpressionStack:
        type_ = debug::ScopeIterator::ScopeTypeLocal;
        break;
      case ScopeTypeLocal:
        type_ = debug::ScopeIterator::ScopeTypeModule;
        break;
      case ScopeTypeModule:
        // We use ScopeTypeWith type as marker for done.
        type_ = debug::ScopeIterator::ScopeTypeWith;
        break;
      default:
        UNREACHABLE();
    }
  }

  ScopeType GetType() override { return type_; }

  v8::Local<v8::Object> GetObject() override {
    Isolate* isolate = frame_->isolate();
    switch (type_) {
      case debug::ScopeIterator::ScopeTypeModule: {
        Handle<WasmInstanceObject> instance{frame_->wasm_instance(), isolate};
        Handle<JSObject> object =
            isolate->factory()->NewSlowJSObjectWithNullProto();
        JSObject::AddProperty(isolate, object, "instance", instance, FROZEN);
        DirectHandle<JSObject> module_object(instance->module_object(),
                                             isolate);
        JSObject::AddProperty(isolate, object, "module", module_object, FROZEN);
        if (FunctionsProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "functions",
              GetOrCreateInstanceProxy<FunctionsProxy>(isolate, instance),
              FROZEN);
        }
        if (GlobalsProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "globals",
              GetOrCreateInstanceProxy<GlobalsProxy>(isolate, instance),
              FROZEN);
        }
        if (MemoriesProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "memories",
              GetOrCreateInstanceProxy<MemoriesProxy>(isolate, instance),
              FROZEN);
        }
        if (TablesProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "tables",
              GetOrCreateInstanceProxy<TablesProxy>(isolate, instance), FROZEN);
        }
        return Utils::ToLocal(object);
      }
      case debug::ScopeIterator::ScopeTypeLocal: {
        return Utils::ToLocal(LocalsProxy::Create(frame_));
      }
      case debug::ScopeIterator::ScopeTypeWasmExpressionStack: {
        auto object = isolate->factory()->NewSlowJSObjectWithNullProto();
        auto stack = StackProxy::Create(frame_);
        JSObject::AddProperty(isolate, object, "stack", stack, FROZEN);
        return Utils::ToLocal(object);
      }
      default:
        UNREACHABLE();
    }
  }
  v8::Local<v8::Value> GetFunctionDebugName() override {
    return Utils::ToLocal(frame_->isolate()->factory()->empty_string());
  }

  int GetScriptId() override { return -1; }

  bool HasLocationInfo() override { return false; }

  debug::Location GetStartLocation() override { return {}; }

  debug::Location GetEndLocation() override { return {}; }

  bool SetVariableValue(v8::Local<v8::String> name,
                        v8::Local<v8::Value> value) override {
    return false;
  }

 private:
  WasmFrame* const frame_;
  ScopeType type_;
};

#if V8_ENABLE_DRUMBRAKE
class DebugWasmInterpreterScopeIterator final : public debug::ScopeIterator {
 public:
  explicit DebugWasmInterpreterScopeIterator(WasmInterpreterEntryFrame* frame)
      : frame_(frame), type_(debug::ScopeIterator::ScopeTypeModule) {
    // TODO(paolosev@microsoft.com) -  Enable local scopes and expression stack
    // scopes.
  }

  bool Done() override { return type_ == ScopeTypeWith; }

  void Advance() override {
    DCHECK(!Done());
    switch (type_) {
      case ScopeTypeModule:
        // We use ScopeTypeWith type as marker for done.
        type_ = debug::ScopeIterator::ScopeTypeWith;
        break;
      case ScopeTypeWasmExpressionStack:
      case ScopeTypeLocal:
      default:
        UNREACHABLE();
    }
  }

  ScopeType GetType() override { return type_; }

  v8::Local<v8::Object> GetObject() override {
    Isolate* isolate = frame_->isolate();
    switch (type_) {
      case debug::ScopeIterator::ScopeTypeModule: {
        Handle<WasmInstanceObject> instance(frame_->wasm_instance(), isolate);
        Handle<JSObject> object =
            isolate->factory()->NewSlowJSObjectWithNullProto();
        JSObject::AddProperty(isolate, object, "instance", instance, FROZEN);
        Handle<JSObject> module_object(instance->module_object(), isolate);
        JSObject::AddProperty(isolate, object, "module", module_object, FROZEN);
        if (FunctionsProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "functions",
              GetOrCreateInstanceProxy<FunctionsProxy>(isolate, instance),
              FROZEN);
        }
        if (GlobalsProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "globals",
              GetOrCreateInstanceProxy<GlobalsProxy>(isolate, instance),
              FROZEN);
        }
        if (MemoriesProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "memories",
              GetOrCreateInstanceProxy<MemoriesProxy>(isolate, instance),
              FROZEN);
        }
        if (TablesProxy::Count(isolate, instance) != 0) {
          JSObject::AddProperty(
              isolate, object, "tables",
              GetOrCreateInstanceProxy<TablesProxy>(isolate, instance), FROZEN);
        }
        return Utils::ToLocal(object);
      }
      case debug::ScopeIterator::ScopeTypeLocal:
      case debug::ScopeIterator::ScopeTypeWasmExpressionStack:
      default:
        UNREACHABLE();
    }
  }
  v8::Local<v8::Value> GetFunctionDebugName() override {
    return Utils::ToLocal(frame_->isolate()->factory()->empty_string());
  }

  int GetScriptId() override { return -1; }

  bool HasLocationInfo() override { return false; }

  debug::Location GetStartLocation() override { return {}; }

  debug::Location GetEndLocation() override { return {}; }

  bool SetVariableValue(v8::Local<v8::String> name,
                        v8::Local<v8::Value> value) override {
    return false;
  }

 private:
  WasmInterpreterEntryFrame* const frame_;
  ScopeType type_;
};
#endif  // V8_ENABLE_DRUMBRAKE

Handle<String> WasmSimd128ToString(Isolate* isolate, Simd128 s128) {
  // We use the canonical format as described in:
  // https://github.com/WebAssembly/simd/blob/master/proposals/simd/TextSIMD.md
  base::EmbeddedVector<char, 50> buffer;
  auto i32x4 = s128.to_i32x4();
  SNPrintF(buffer, "i32x4 0x%08X 0x%08X 0x%08X 0x%08X", i32x4.val[0],
           i32x4.val[1], i32x4.val[2], i32x4.val[3]);
  return isolate->factory()->NewStringFromAsciiChecked(buffer.data());
}

Handle<String> GetRefTypeName(Isolate* isolate, wasm::ValueType type,
                              wasm::NativeModule* module) {
  DCHECK(type.is_object_reference());
  StringBuilder name;
  module->GetNamesProvider()->PrintValueType(name, type);
  return ToInternalString(name, isolate);
}

// Returns the type name for the given value. Uses the module object for
// providing user-defined type names if available, otherwise falls back
// to numbers for indexed types.
Handle<String> GetRefTypeName(Isolate* isolate, wasm::ValueType type,
                              Handle<WasmModuleObject> module_object) {
  if (!module_object.is_null()) {
    return GetRefTypeName(isolate, type, module_object->native_module());
  }
  std::string name = type.name();
  return isolate->factory()->InternalizeString({name.data(), name.length()});
}

}  // namespace

// static
Handle<WasmValueObject> WasmValueObject::New(Isolate* isolate,
                                             DirectHandle<String> type,
                                             DirectHandle<Object> value) {
  auto maps = GetOrCreateDebugMaps(isolate);
  if (maps->is_the_hole(isolate, kWasmValueMapIndex)) {
    DirectHandle<Map> map =
        isolate->factory()->NewContextfulMapForCurrentContext(
            WASM_VALUE_OBJECT_TYPE, WasmValueObject::kSize,
            TERMINAL_FAST_ELEMENTS_KIND, 2);
    Map::EnsureDescriptorSlack(isolate, map, 2);
    map->SetConstructor(*isolate->object_function());
    {  // type
      Descriptor d = Descriptor::DataField(
          isolate,
          isolate->factory()->InternalizeString(base::StaticCharVector("type")),
          WasmValueObject::kTypeIndex, FROZEN, Representation::Tagged());
      map->AppendDescriptor(isolate, &d);
    }
    {  // value
      Descriptor d = Descriptor::DataField(
          isolate,
          isolate->factory()->InternalizeString(
              base::StaticCharVector("value")),
          WasmValueObject::kValueIndex, FROZEN, Representation::Tagged());
      map->AppendDescriptor(isolate, &d);
    }
    map->set_is_extensible(false);
    maps->set(kWasmValueMapIndex, *map);
  }
  DirectHandle<Map> value_map(Cast<Map>(maps->get(kWasmValueMapIndex)),
                              isolate);
  auto object =
      Cast<WasmValueObject>(isolate->factory()->NewJSObjectFromMap(value_map));
  object->set_type(*type);
  object->set_value(*value);
  return object;
}

// This class implements a proxy for a single inspectable Wasm struct.
struct StructProxy : NamedDebugProxy<StructProxy, kStructProxy, FixedArray> {
  static constexpr char const* kClassName = "Struct";

  static const int kObjectIndex = 0;
  static const int kModuleIndex = 1;
  static const int kTypeIndexIndex = 2;
  static const int kLength = 3;

  static Handle<JSObject> Create(Isolate* isolate,
                                 DirectHandle<WasmStruct> value,
                                 DirectHandle<WasmModuleObject> module) {
    Handle<FixedArray> data = isolate->factory()->NewFixedArray(kLength);
    data->set(kObjectIndex, *value);
    data->set(kModuleIndex, *module);
    int struct_type_index = value->map()->wasm_type_info()->type_index();
    data->set(kTypeIndexIndex, Smi::FromInt(struct_type_index));
    return NamedDebugProxy::Create(isolate, data);
  }

  static uint32_t Count(Isolate* isolate, DirectHandle<FixedArray> data) {
    return Cast<WasmStruct>(data->get(kObjectIndex))->type()->field_count();
  }

  static Handle<Object> Get(Isolate* isolate, DirectHandle<FixedArray> data,
                            uint32_t index) {
    DirectHandle<WasmStruct> obj(Cast<WasmStruct>(data->get(kObjectIndex)),
                                 isolate);
    Handle<WasmModuleObject> module(
        Cast<WasmModuleObject>(data->get(kModuleIndex)), isolate);
    return WasmValueObject::New(isolate, obj->GetFieldValue(index), module);
  }

  static Handle<String> GetName(Isolate* isolate, DirectHandle<FixedArray> data,
                                uint32_t index) {
    wasm::NativeModule* native_module =
        Cast<WasmModuleObject>(data->get(kModuleIndex))->native_module();
    int struct_type_index = Smi::ToInt(Cast<Smi>(data->get(kTypeIndexIndex)));
    wasm::NamesProvider* names = native_module->GetNamesProvider();
    StringBuilder sb;
    names->PrintFieldName(sb, struct_type_index, index);
    return ToInternalString(sb, isolate);
  }
};

// This class implements a proxy for a single inspectable Wasm array.
struct ArrayProxy : IndexedDebugProxy<ArrayProxy, kArrayProxy, FixedArray> {
  static constexpr char const* kClassName = "Array";

  static const int kObjectIndex = 0;
  static const int kModuleIndex = 1;
  static const int kLength = 2;

  static Handle<JSObject> Create(Isolate* isolate,
                                 DirectHandle<WasmArray> value,
                                 DirectHandle<WasmModuleObject> module) {
    Handle<FixedArray> data = isolate->factory()->NewFixedArray(kLength);
    data->set(kObjectIndex, *value);
    data->set(kModuleIndex, *module);
    Handle<JSObject> proxy = IndexedDebugProxy::Create(
        isolate, data, false /* leave map extensible */);
    uint32_t length = value->length();
    Handle<Object> length_obj = isolate->factory()->NewNumberFromUint(length);
    Object::SetProperty(isolate, proxy, isolate->factory()->length_string(),
                        length_obj, StoreOrigin::kNamed,
                        Just(ShouldThrow::kThrowOnError))
        .Check();
    return proxy;
  }

  static v8::Local<v8::FunctionTemplate> CreateTemplate(v8::Isolate* isolate) {
    Local<v8::FunctionTemplate> templ =
        IndexedDebugProxy::CreateTemplate(isolate);
    templ->InstanceTemplate()->Set(isolate, "length",
                                   v8::Number::New(isolate, 0));
    return templ;
  }

  static uint32_t Count(Isolate* isolate, DirectHandle<FixedArray> data) {
    return Cast<WasmArray>(data->get(kObjectIndex))->length();
  }

  static Handle<Object> Get(Isolate* isolate, DirectHandle<FixedArray> data,
                            uint32_t index) {
    DirectHandle<WasmArray> array(Cast<WasmArray>(data->get(kObjectIndex)),
                                  isolate);
    Handle<WasmModuleObject> module(
        Cast<WasmModuleObject>(data->get(kModuleIndex)), isolate);
    return WasmValueObject::New(isolate, array->GetElement(index), module);
  }
};

// static
Handle<WasmValueObject> WasmValueObject::New(
    Isolate* isolate, const wasm::WasmValue& value,
    Handle<WasmModuleObject> module_object) {
  DirectHandle<String> t;
  DirectHandle<Object> v;
  switch (value.type().kind()) {
    case wasm::kI8: {
      // This can't be reached for most "top-level" things, only via nested
      // calls for struct/array fields.
      t = isolate->factory()->InternalizeString(base::StaticCharVector("i8"));
      v = isolate->factory()->NewNumber(value.to_i8_unchecked());
      break;
    }
    case wasm::kI16: {
      // This can't be reached for most "top-level" things, only via nested
      // calls for struct/array fields.
      t = isolate->factory()->InternalizeString(base::StaticCharVector("i16"));
      v = isolate->factory()->NewNumber(value.to_i16_unchecked());
      break;
    }
    case wasm::kI32: {
      t = isolate->factory()->InternalizeString(base::StaticCharVector("i32"));
      v = isolate->factory()->NewNumberFromInt(value.to_i32_unchecked());
      break;
    }
    case wasm::kI64: {
      t = isolate->factory()->InternalizeString(base::StaticCharVector("i64"));
      v = BigInt::FromInt64(isolate, value.to_i64_unchecked());
      break;
    }
    case wasm::kF16: {
      // This can't be reached for most "top-level" things, only via nested
      // calls for struct/array fields.
      t = isolate->factory()->InternalizeString(base::StaticCharVector("f16"));
      v = isolate->factory()->NewNumber(value.to_f16_unchecked());
      break;
    }
    case wasm::kF32: {
      t = isolate->factory()->InternalizeString(base::StaticCharVector("f32"));
      v = isolate->factory()->NewNumber(value.to_f32_unchecked());
      break;
    }
    case wasm::kF64: {
      t = isolate->factory()->InternalizeString(base::StaticCharVector("f64"));
      v = isolate->factory()->NewNumber(value.to_f64_unchecked());
      break;
    }
    case wasm::kS128: {
      t = isolate->factory()->InternalizeString(base::StaticCharVector("v128"));
      v = WasmSimd128ToString(isolate, value.to_s128_unchecked());
      break;
    }
    case wasm::kRefNull:
    case wasm::kRef: {
      Handle<Object> ref = value.to_ref();
      if (value.type().heap_type().representation() == wasm::HeapType::kExn) {
        t = isolate->factory()->InternalizeString(
            base::StaticCharVector("exnref"));
        v = ref;
      } else if (IsWasmStruct(*ref)) {
        Tagged<WasmTypeInfo> type_info =
            Cast<HeapObject>(*ref)->map()->wasm_type_info();
        wasm::ValueType type = wasm::ValueType::FromIndex(
            wasm::ValueKind::kRef, type_info->type_index());
        // Getting the trusted data is safe; structs always have the instance
        // data defined.
        DirectHandle<WasmTrustedInstanceData> wtid(
            type_info->trusted_data(isolate), isolate);
        t = GetRefTypeName(isolate, type, wtid->native_module());
        v = StructProxy::Create(isolate, Cast<WasmStruct>(ref),
                                direct_handle(wtid->module_object(), isolate));
      } else if (IsWasmArray(*ref)) {
        Tagged<WasmTypeInfo> type_info =
            Cast<HeapObject>(*ref)->map()->wasm_type_info();
        wasm::ValueType type = wasm::ValueType::FromIndex(
            wasm::ValueKind::kRef, type_info->type_index());
        // Getting the trusted data is safe; arrays always have the instance
        // data defined.
        DirectHandle<WasmTrustedInstanceData> wtid(
            type_info->trusted_data(isolate), isolate);
        t = GetRefTypeName(isolate, type, wtid->native_module());
        v = ArrayProxy::Create(isolate, Cast<WasmArray>(ref),
                               direct_handle(wtid->module_object(), isolate));
      } else if (IsWasmFuncRef(*ref)) {
        DirectHandle<WasmInternalFunction> internal_fct{
            Cast<WasmFuncRef>(*ref)->internal(isolate), isolate};
        v = WasmInternalFunction::GetOrCreateExternal(internal_fct);
        // If the module is not provided by the caller, retrieve it from the
        // instance object. If the function was created in JavaScript using
        // `new WebAssembly.Function(...)`, a module for name resolution is not
        // available.
        if (module_object.is_null() &&
            IsWasmTrustedInstanceData(internal_fct->implicit_arg())) {
          module_object =
              handle(Cast<WasmTrustedInstanceData>(internal_fct->implicit_arg())
                         ->module_object(),
                     isolate);
        }
        t = GetRefTypeName(isolate, value.type(), module_object);
      } else if (IsWasmNull(*ref)) {
        // TODO(manoskouk): Is this value correct?
        v = isolate->factory()->null_value();
        t = GetRefTypeName(isolate, value.type(), module_object);
      } else if (IsJSFunction(*ref) || IsSmi(*ref) || IsNull(*ref) ||
                 IsString(*ref) ||
                 value.type().is_reference_to(wasm::HeapType::kExtern) ||
                 value.type().is_reference_to(wasm::HeapType::kAny)) {
        t = GetRefTypeName(isolate, value.type(), module_object);
        v = ref;
      } else {
        // Fail gracefully.
        base::EmbeddedVector<char, 64> error;
        int len = SNPrintF(error, "unimplemented object type: %d",
                           Cast<HeapObject>(*ref)->map()->instance_type());
        t = GetRefTypeName(isolate, value.type(), module_object);
        v = isolate->factory()->InternalizeString(error.SubVector(0, len));
      }
      break;
    }
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kBottom:
      UNREACHABLE();
  }
  return New(isolate, t, v);
}

Handle<JSObject> GetWasmDebugProxy(WasmFrame* frame) {
  return ContextProxy::Create(frame);
}

std::unique_ptr<debug::ScopeIterator> GetWasmScopeIterator(WasmFrame* frame) {
  return std::make_unique<DebugWasmScopeIterator>(frame);
}

#if V8_ENABLE_DRUMBRAKE
std::unique_ptr<debug::ScopeIterator> GetWasmInterpreterScopeIterator(
    WasmInterpreterEntryFrame* frame) {
  return std::make_unique<DebugWasmInterpreterScopeIterator>(frame);
}
#endif  // V8_ENABLE_DRUMBRAKE

Handle<String> GetWasmFunctionDebugName(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance_data,
    uint32_t func_index) {
  wasm::NativeModule* native_module = instance_data->native_module();
  wasm::NamesProvider* names = native_module->GetNamesProvider();
  StringBuilder sb;
  wasm::NamesProvider::FunctionNamesBehavior behavior =
      is_asmjs_module(native_module->module())
          ? wasm::NamesProvider::kWasmInternal
          : wasm::NamesProvider::kDevTools;
  names->PrintFunctionName(sb, func_index, behavior);
  return ToInternalString(sb, isolate);
}

Handle<ArrayList> AddWasmInstanceObjectInternalProperties(
    Isolate* isolate, Handle<ArrayList> result,
    Handle<WasmInstanceObject> instance) {
  result = ArrayList::Add(
      isolate, result,
      isolate->factory()->NewStringFromAsciiChecked("[[Module]]"),
      handle(instance->module_object(), isolate));

  if (FunctionsProxy::Count(isolate, instance) != 0) {
    result = ArrayList::Add(
        isolate, result,
        isolate->factory()->NewStringFromAsciiChecked("[[Functions]]"),
        GetOrCreateInstanceProxy<FunctionsProxy>(isolate, instance));
  }

  if (GlobalsProxy::Count(isolate, instance) != 0) {
    result = ArrayList::Add(
        isolate, result,
        isolate->factory()->NewStringFromAsciiChecked("[[Globals]]"),
        GetOrCreateInstanceProxy<GlobalsProxy>(isolate, instance));
  }

  if (MemoriesProxy::Count(isolate, instance) != 0) {
    result = ArrayList::Add(
        isolate, result,
        isolate->factory()->NewStringFromAsciiChecked("[[Memories]]"),
        GetOrCreateInstanceProxy<MemoriesProxy>(isolate, instance));
  }

  if (TablesProxy::Count(isolate, instance) != 0) {
    result = ArrayList::Add(
        isolate, result,
        isolate->factory()->NewStringFromAsciiChecked("[[Tables]]"),
        GetOrCreateInstanceProxy<TablesProxy>(isolate, instance));
  }

  return result;
}

Handle<ArrayList> AddWasmModuleObjectInternalProperties(
    Isolate* isolate, Handle<ArrayList> result,
    DirectHandle<WasmModuleObject> module_object) {
  result = ArrayList::Add(
      isolate, result,
      isolate->factory()->NewStringFromStaticChars("[[Exports]]"),
      wasm::GetExports(isolate, module_object));
  result = ArrayList::Add(
      isolate, result,
      isolate->factory()->NewStringFromStaticChars("[[Imports]]"),
      wasm::GetImports(isolate, module_object));
  return result;
}

Handle<ArrayList> AddWasmTableObjectInternalProperties(
    Isolate* isolate, Handle<ArrayList> result,
    DirectHandle<WasmTableObject> table) {
  int length = table->current_length();
  DirectHandle<FixedArray> entries = isolate->factory()->NewFixedArray(length);
  for (int i = 0; i < length; ++i) {
    Handle<Object> entry = WasmTableObject::Get(isolate, table, i);
    wasm::WasmValue wasm_value(entry, table->type());
    Handle<WasmModuleObject> module;
    if (table->has_trusted_data()) {
      module = Handle<WasmModuleObject>(
          table->trusted_data(isolate)->module_object(), isolate);
    }
    DirectHandle<Object> debug_value =
        WasmValueObject::New(isolate, wasm_value, module);
    entries->set(i, *debug_value);
  }
  Handle<JSArray> final_entries = isolate->factory()->NewJSArrayWithElements(
      entries, i::PACKED_ELEMENTS, length);
  JSObject::SetPrototype(isolate, final_entries,
                         isolate->factory()->null_value(), false, kDontThrow)
      .Check();
  DirectHandle<String> entries_string =
      isolate->factory()->NewStringFromStaticChars("[[Entries]]");
  result = ArrayList::Add(isolate, result, entries_string, final_entries);
  return result;
}

}  // namespace internal
}  // namespace v8

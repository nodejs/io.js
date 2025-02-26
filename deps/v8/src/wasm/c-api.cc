// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This implementation is originally from
// https://github.com/WebAssembly/wasm-c-api/:

// Copyright 2019 Andreas Rossberg
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/wasm/c-api.h"

#include <cstring>
#include <iomanip>
#include <iostream>

#include "include/libplatform/libplatform.h"
#include "include/v8-initialization.h"
#include "src/api/api-inl.h"
#include "src/builtins/builtins.h"
#include "src/compiler/wasm-compiler.h"
#include "src/flags/flags.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/managed-inl.h"
#include "src/wasm/leb-helper.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/signature-hashing.h"
#include "src/wasm/wasm-arguments.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-result.h"
#include "src/wasm/wasm-serialization.h"
#include "third_party/wasm-api/wasm.h"

#ifdef V8_OS_WIN

// Setup for Windows DLL export/import. When building the V8 DLL the
// BUILDING_V8_SHARED needs to be defined. When building a program which uses
// the V8 DLL USING_V8_SHARED needs to be defined. When either building the V8
// static library or building a program which uses the V8 static library neither
// BUILDING_V8_SHARED nor USING_V8_SHARED should be defined.
#if !defined(LIBWASM_STATIC) && defined(USING_V8_SHARED)
#define WASM_EXPORT __declspec(dllimport)
#elif !defined(LIBWASM_STATIC)
#define WASM_EXPORT __declspec(dllexport)
#else
#define WASM_EXPORT
#endif  // BUILDING_V8_SHARED

#else  // V8_OS_WIN

// Setup for Linux shared library export.
#if V8_HAS_ATTRIBUTE_VISIBILITY
#ifdef BUILDING_V8_SHARED
#define WASM_EXPORT __attribute__((visibility("default")))
#else
#define WASM_EXPORT
#endif
#else
#define WASM_EXPORT
#endif

#endif  // V8_OS_WIN

#ifdef ENABLE_VTUNE_JIT_INTERFACE
#include "third_party/vtune/v8-vtune.h"
#endif

#ifdef WASM_API_DEBUG
#error "WASM_API_DEBUG is unsupported"
#endif

// If you want counters support (what --dump-counters does for the d8 shell),
// then set this to 1 (in here, or via -DDUMP_COUNTERS=1 compiler argument).
#define DUMP_COUNTERS 0

namespace wasm {

namespace {

// Multi-cage pointer compression mode related note.
// Wasm C-Api is allowed to be used from a thread that's not bound to any
// Isolate. As a result, in a multi-cage pointer compression mode it's not
// guaranteed that current pointer compression cage base value is initialized
// for current thread (see V8HeapCompressionScheme::base_) which makes it
// impossible to read compressed pointers from V8 heap objects.
// This scope ensures that the pointer compression base value is set according
// to respective Wasm C-Api object.
// For all other configurations this scope is a no-op.
using PtrComprCageAccessScope = i::PtrComprCageAccessScope;

auto ReadLebU64(const byte_t** pos) -> uint64_t {
  uint64_t n = 0;
  uint64_t shift = 0;
  byte_t b;
  do {
    b = **pos;
    (*pos)++;
    n += (b & 0x7f) << shift;
    shift += 7;
  } while ((b & 0x80) != 0);
  return n;
}

template <typename T>
ValKind V8ValueTypeToWasm(T v8_valtype)
  requires(std::is_same_v<T, i::wasm::ValueType> ||
           std::is_same_v<T, i::wasm::CanonicalValueType>)
{
  switch (v8_valtype.kind()) {
    case i::wasm::kI32:
      return ValKind::I32;
    case i::wasm::kI64:
      return ValKind::I64;
    case i::wasm::kF32:
      return ValKind::F32;
    case i::wasm::kF64:
      return ValKind::F64;
    case i::wasm::kRef:
    case i::wasm::kRefNull:
      switch (v8_valtype.heap_representation()) {
        case i::wasm::HeapType::kFunc:
          return ValKind::FUNCREF;
        case i::wasm::HeapType::kExtern:
          return ValKind::EXTERNREF;
        default:
          UNREACHABLE();
      }
    default:
      UNREACHABLE();
  }
}

i::wasm::ValueType WasmValKindToV8(ValKind kind) {
  switch (kind) {
    case ValKind::I32:
      return i::wasm::kWasmI32;
    case ValKind::I64:
      return i::wasm::kWasmI64;
    case ValKind::F32:
      return i::wasm::kWasmF32;
    case ValKind::F64:
      return i::wasm::kWasmF64;
    case ValKind::FUNCREF:
      return i::wasm::kWasmFuncRef;
    case ValKind::EXTERNREF:
      return i::wasm::kWasmExternRef;
    default:
      // TODO(wasm+): support new value types
      UNREACHABLE();
  }
}

Name GetNameFromWireBytes(const i::wasm::WireBytesRef& ref,
                          v8::base::Vector<const uint8_t> wire_bytes) {
  DCHECK_LE(ref.offset(), wire_bytes.length());
  DCHECK_LE(ref.end_offset(), wire_bytes.length());
  if (ref.length() == 0) return Name::make();
  Name name = Name::make_uninitialized(ref.length());
  std::memcpy(name.get(), wire_bytes.begin() + ref.offset(), ref.length());
  return name;
}

own<FuncType> FunctionSigToFuncType(const i::wasm::FunctionSig* sig) {
  size_t param_count = sig->parameter_count();
  ownvec<ValType> params = ownvec<ValType>::make_uninitialized(param_count);
  for (size_t i = 0; i < param_count; i++) {
    params[i] = ValType::make(V8ValueTypeToWasm(sig->GetParam(i)));
  }
  size_t return_count = sig->return_count();
  ownvec<ValType> results = ownvec<ValType>::make_uninitialized(return_count);
  for (size_t i = 0; i < return_count; i++) {
    results[i] = ValType::make(V8ValueTypeToWasm(sig->GetReturn(i)));
  }
  return FuncType::make(std::move(params), std::move(results));
}

own<ExternType> GetImportExportType(const i::wasm::WasmModule* module,
                                    const i::wasm::ImportExportKindCode kind,
                                    const uint32_t index) {
  switch (kind) {
    case i::wasm::kExternalFunction: {
      return FunctionSigToFuncType(module->functions[index].sig);
    }
    case i::wasm::kExternalTable: {
      const i::wasm::WasmTable& table = module->tables[index];
      own<ValType> elem = ValType::make(V8ValueTypeToWasm(table.type));
      Limits limits(table.initial_size,
                    table.has_maximum_size
                        ? v8::base::checked_cast<int32_t>(table.maximum_size)
                        : -1);
      return TableType::make(std::move(elem), limits);
    }
    case i::wasm::kExternalMemory: {
      const i::wasm::WasmMemory& memory = module->memories[index];
      Limits limits(memory.initial_pages,
                    memory.has_maximum_pages
                        ? v8::base::checked_cast<int32_t>(memory.maximum_pages)
                        : -1);
      return MemoryType::make(limits);
    }
    case i::wasm::kExternalGlobal: {
      const i::wasm::WasmGlobal& global = module->globals[index];
      own<ValType> content = ValType::make(V8ValueTypeToWasm(global.type));
      Mutability mutability =
          global.mutability ? Mutability::VAR : Mutability::CONST;
      return GlobalType::make(std::move(content), mutability);
    }
    case i::wasm::kExternalTag:
      UNREACHABLE();
  }
}

}  // namespace

/// BEGIN FILE wasm-v8.cc

///////////////////////////////////////////////////////////////////////////////
// Auxiliaries

[[noreturn]] void WASM_UNIMPLEMENTED(const char* s) {
  std::cerr << "Wasm API: " << s << " not supported yet!\n";
  exit(1);
}

template <class T>
void ignore(T) {}

template <class C>
struct implement;

template <class C>
auto impl(C* x) -> typename implement<C>::type* {
  return reinterpret_cast<typename implement<C>::type*>(x);
}

template <class C>
auto impl(const C* x) -> const typename implement<C>::type* {
  return reinterpret_cast<const typename implement<C>::type*>(x);
}

template <class C>
auto seal(typename implement<C>::type* x) -> C* {
  return reinterpret_cast<C*>(x);
}

template <class C>
auto seal(const typename implement<C>::type* x) -> const C* {
  return reinterpret_cast<const C*>(x);
}

///////////////////////////////////////////////////////////////////////////////
// Runtime Environment

// Configuration

struct ConfigImpl {};

template <>
struct implement<Config> {
  using type = ConfigImpl;
};

WASM_EXPORT void Config::destroy() { delete impl(this); }

WASM_EXPORT auto Config::make() -> own<Config> {
  return own<Config>(seal<Config>(new (std::nothrow) ConfigImpl()));
}

// Engine

#if DUMP_COUNTERS
class Counter {
 public:
  static const int kMaxNameSize = 64;
  int32_t* Bind(const char* name, bool is_histogram) {
    int i;
    for (i = 0; i < kMaxNameSize - 1 && name[i]; i++) {
      name_[i] = static_cast<char>(name[i]);
    }
    name_[i] = '\0';
    is_histogram_ = is_histogram;
    return ptr();
  }
  int32_t* ptr() { return &count_; }
  int32_t count() { return count_; }
  int32_t sample_total() { return sample_total_; }
  bool is_histogram() { return is_histogram_; }
  void AddSample(int32_t sample) {
    count_++;
    sample_total_ += sample;
  }

 private:
  int32_t count_;
  int32_t sample_total_;
  bool is_histogram_;
  uint8_t name_[kMaxNameSize];
};

class CounterCollection {
 public:
  CounterCollection() = default;
  Counter* GetNextCounter() {
    if (counters_in_use_ == kMaxCounters) return nullptr;
    return &counters_[counters_in_use_++];
  }

 private:
  static const unsigned kMaxCounters = 512;
  uint32_t counters_in_use_{0};
  Counter counters_[kMaxCounters];
};

using CounterMap = std::unordered_map<std::string, Counter*>;

#endif

struct EngineImpl {
  static bool created;

  std::unique_ptr<v8::Platform> platform;

#if DUMP_COUNTERS
  static CounterCollection counters_;
  static CounterMap* counter_map_;

  static Counter* GetCounter(const char* name, bool is_histogram) {
    auto map_entry = counter_map_->find(name);
    Counter* counter =
        map_entry != counter_map_->end() ? map_entry->second : nullptr;

    if (counter == nullptr) {
      counter = counters_.GetNextCounter();
      if (counter != nullptr) {
        (*counter_map_)[name] = counter;
        counter->Bind(name, is_histogram);
      }
    } else {
      DCHECK(counter->is_histogram() == is_histogram);
    }
    return counter;
  }

  static int* LookupCounter(const char* name) {
    Counter* counter = GetCounter(name, false);

    if (counter != nullptr) {
      return counter->ptr();
    } else {
      return nullptr;
    }
  }

  static void* CreateHistogram(const char* name, int min, int max,
                               size_t buckets) {
    return GetCounter(name, true);
  }

  static void AddHistogramSample(void* histogram, int sample) {
    Counter* counter = reinterpret_cast<Counter*>(histogram);
    counter->AddSample(sample);
  }
#endif

  EngineImpl() {
    assert(!created);
    created = true;
#if DUMP_COUNTERS
    counter_map_ = new CounterMap();
#endif
  }

  ~EngineImpl() {
#if DUMP_COUNTERS
    std::vector<std::pair<std::string, Counter*>> counters(
        counter_map_->begin(), counter_map_->end());
    std::sort(counters.begin(), counters.end());
    // Dump counters in formatted boxes.
    constexpr int kNameBoxSize = 64;
    constexpr int kValueBoxSize = 13;
    std::cout << "+" << std::string(kNameBoxSize, '-') << "+"
              << std::string(kValueBoxSize, '-') << "+\n";
    std::cout << "| Name" << std::string(kNameBoxSize - 5, ' ') << "| Value"
              << std::string(kValueBoxSize - 6, ' ') << "|\n";
    std::cout << "+" << std::string(kNameBoxSize, '-') << "+"
              << std::string(kValueBoxSize, '-') << "+\n";
    for (const auto& pair : counters) {
      std::string key = pair.first;
      Counter* counter = pair.second;
      if (counter->is_histogram()) {
        std::cout << "| c:" << std::setw(kNameBoxSize - 4) << std::left << key
                  << " | " << std::setw(kValueBoxSize - 2) << std::right
                  << counter->count() << " |\n";
        std::cout << "| t:" << std::setw(kNameBoxSize - 4) << std::left << key
                  << " | " << std::setw(kValueBoxSize - 2) << std::right
                  << counter->sample_total() << " |\n";
      } else {
        std::cout << "| " << std::setw(kNameBoxSize - 2) << std::left << key
                  << " | " << std::setw(kValueBoxSize - 2) << std::right
                  << counter->count() << " |\n";
      }
    }
    std::cout << "+" << std::string(kNameBoxSize, '-') << "+"
              << std::string(kValueBoxSize, '-') << "+\n";
    delete counter_map_;
#endif
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
  }
};

bool EngineImpl::created = false;

#if DUMP_COUNTERS
CounterCollection EngineImpl::counters_;
CounterMap* EngineImpl::counter_map_;
#endif

template <>
struct implement<Engine> {
  using type = EngineImpl;
};

WASM_EXPORT void Engine::destroy() { delete impl(this); }

WASM_EXPORT auto Engine::make(own<Config>&& config) -> own<Engine> {
  auto engine = new (std::nothrow) EngineImpl;
  if (!engine) return own<Engine>();
  engine->platform = i::v8_flags.single_threaded
                         ? v8::platform::NewSingleThreadedDefaultPlatform()
                         : v8::platform::NewDefaultPlatform(
                               i::v8_flags.wasm_capi_thread_pool_size);
  v8::V8::InitializePlatform(engine->platform.get());
  v8::V8::Initialize();

  if (i::v8_flags.prof) {
    i::PrintF(
        "--prof is currently unreliable for V8's Wasm-C-API due to "
        "fast-c-calls.\n");
  }

  return make_own(seal<Engine>(engine));
}

// This should be called somewhat regularly, especially on potentially hot
// sections of pure C++ execution. To achieve that, we call it on API entry
// points that heap-allocate but don't call into generated code.
// For example, finalization of incremental marking is relying on it.
void CheckAndHandleInterrupts(i::Isolate* isolate) {
  i::StackLimitCheck check(isolate);
  if (check.InterruptRequested()) {
    isolate->stack_guard()->HandleInterrupts();
  }
}

// Stores

void StoreImpl::destroy() { delete this; }

StoreImpl::~StoreImpl() {
  {
    v8::Isolate::Scope isolate_scope(isolate_);
#ifdef DEBUG
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate_);
    i_isolate->heap()->PreciseCollectAllGarbage(
        i::GCFlag::kForced, i::GarbageCollectionReason::kTesting,
        v8::kNoGCCallbackFlags);
#endif
    context()->Exit();
  }
  isolate_->Dispose();
  delete create_params_.array_buffer_allocator;
}

struct ManagedData {
  static constexpr i::ExternalPointerTag kManagedTag = i::kWasmManagedDataTag;

  ManagedData(void* info, void (*finalizer)(void*))
      : info(info), finalizer(finalizer) {}

  ~ManagedData() {
    if (finalizer) (*finalizer)(info);
  }

  void* info;
  void (*finalizer)(void*);
};

void StoreImpl::SetHostInfo(i::DirectHandle<i::Object> object, void* info,
                            void (*finalizer)(void*)) {
  v8::Isolate::Scope isolate_scope(isolate());
  i::HandleScope scope(i_isolate());
  // Ideally we would specify the total size kept alive by {info} here,
  // but all we get from the embedder is a {void*}, so our best estimate
  // is the size of the metadata.
  size_t estimated_size = sizeof(ManagedData);
  i::DirectHandle<i::Object> wrapper = i::Managed<ManagedData>::From(
      i_isolate(), estimated_size,
      std::make_shared<ManagedData>(info, finalizer));
  int32_t hash = i::Object::GetOrCreateHash(*object, i_isolate()).value();
  i::JSWeakCollection::Set(host_info_map_, object, wrapper, hash);
}

void* StoreImpl::GetHostInfo(i::DirectHandle<i::Object> key) {
  PtrComprCageAccessScope ptr_compr_cage_access_scope(i_isolate());
  i::Tagged<i::Object> raw =
      i::Cast<i::EphemeronHashTable>(host_info_map_->table())->Lookup(key);
  if (IsTheHole(raw, i_isolate())) return nullptr;
  return i::Cast<i::Managed<ManagedData>>(raw)->raw()->info;
}

template <>
struct implement<Store> {
  using type = StoreImpl;
};

WASM_EXPORT void Store::destroy() { delete impl(this); }

WASM_EXPORT auto Store::make(Engine*) -> own<Store> {
  auto store = make_own(new (std::nothrow) StoreImpl());
  if (!store) return own<Store>();

  // Create isolate.
  store->create_params_.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
#ifdef ENABLE_VTUNE_JIT_INTERFACE
  store->create_params_.code_event_handler = vTune::GetVtuneCodeEventHandler();
#endif
#if DUMP_COUNTERS
  store->create_params_.counter_lookup_callback = EngineImpl::LookupCounter;
  store->create_params_.create_histogram_callback = EngineImpl::CreateHistogram;
  store->create_params_.add_histogram_sample_callback =
      EngineImpl::AddHistogramSample;
#endif
  v8::Isolate* isolate = v8::Isolate::New(store->create_params_);
  if (!isolate) return own<Store>();
  store->isolate_ = isolate;
  isolate->SetData(0, store.get());
  // We intentionally do not call isolate->Enter() here, because that would
  // prevent embedders from using stores with overlapping but non-nested
  // lifetimes. The consequence is that Isolate::Current() is dysfunctional
  // and hence must not be called by anything reachable via this file.

  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);

    // Create context.
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    if (context.IsEmpty()) return own<Store>();
    context->Enter();  // The Exit() call is in ~StoreImpl.
    store->context_ = v8::Eternal<v8::Context>(isolate, context);

    // Create weak map for Refs with host info.
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    store->host_info_map_ = i_isolate->global_handles()->Create(
        *i_isolate->factory()->NewJSWeakMap());
  }
  // We want stack traces for traps.
  constexpr int kStackLimit = 10;
  isolate->SetCaptureStackTraceForUncaughtExceptions(true, kStackLimit,
                                                     v8::StackTrace::kOverview);

  return make_own(seal<Store>(store.release()));
}

///////////////////////////////////////////////////////////////////////////////
// Type Representations

// Value Types

struct ValTypeImpl {
  ValKind kind;

  explicit ValTypeImpl(ValKind kind) : kind(kind) {}
};

template <>
struct implement<ValType> {
  using type = ValTypeImpl;
};

ValTypeImpl* valtype_i32 = new ValTypeImpl(ValKind::I32);
ValTypeImpl* valtype_i64 = new ValTypeImpl(ValKind::I64);
ValTypeImpl* valtype_f32 = new ValTypeImpl(ValKind::F32);
ValTypeImpl* valtype_f64 = new ValTypeImpl(ValKind::F64);
ValTypeImpl* valtype_externref = new ValTypeImpl(ValKind::EXTERNREF);
ValTypeImpl* valtype_funcref = new ValTypeImpl(ValKind::FUNCREF);

WASM_EXPORT void ValType::destroy() { this->~ValType(); }

WASM_EXPORT own<ValType> ValType::make(ValKind k) {
  ValTypeImpl* valtype;
  switch (k) {
    case ValKind::I32:
      valtype = valtype_i32;
      break;
    case ValKind::I64:
      valtype = valtype_i64;
      break;
    case ValKind::F32:
      valtype = valtype_f32;
      break;
    case ValKind::F64:
      valtype = valtype_f64;
      break;
    case ValKind::EXTERNREF:
      valtype = valtype_externref;
      break;
    case ValKind::FUNCREF:
      valtype = valtype_funcref;
      break;
    default:
      // TODO(wasm+): support new value types
      UNREACHABLE();
  }
  return own<ValType>(seal<ValType>(valtype));
}

WASM_EXPORT auto ValType::copy() const -> own<ValType> { return make(kind()); }

WASM_EXPORT auto ValType::kind() const -> ValKind { return impl(this)->kind; }

// Extern Types

struct ExternTypeImpl {
  ExternKind kind;

  explicit ExternTypeImpl(ExternKind kind) : kind(kind) {}
  virtual ~ExternTypeImpl() = default;
};

template <>
struct implement<ExternType> {
  using type = ExternTypeImpl;
};

WASM_EXPORT void ExternType::destroy() { delete impl(this); }

WASM_EXPORT auto ExternType::copy() const -> own<ExternType> {
  switch (kind()) {
    case ExternKind::FUNC:
      return func()->copy();
    case ExternKind::GLOBAL:
      return global()->copy();
    case ExternKind::TABLE:
      return table()->copy();
    case ExternKind::MEMORY:
      return memory()->copy();
  }
}

WASM_EXPORT auto ExternType::kind() const -> ExternKind {
  return impl(this)->kind;
}

// Function Types

struct FuncTypeImpl : ExternTypeImpl {
  ownvec<ValType> params;
  ownvec<ValType> results;

  FuncTypeImpl(ownvec<ValType>& params, ownvec<ValType>& results)
      : ExternTypeImpl(ExternKind::FUNC),
        params(std::move(params)),
        results(std::move(results)) {}
};

template <>
struct implement<FuncType> {
  using type = FuncTypeImpl;
};

WASM_EXPORT void FuncType::destroy() { delete impl(this); }

WASM_EXPORT auto FuncType::make(ownvec<ValType>&& params,
                                ownvec<ValType>&& results) -> own<FuncType> {
  return params && results
             ? own<FuncType>(seal<FuncType>(new (std::nothrow)
                                                FuncTypeImpl(params, results)))
             : own<FuncType>();
}

WASM_EXPORT auto FuncType::copy() const -> own<FuncType> {
  return make(params().deep_copy(), results().deep_copy());
}

WASM_EXPORT auto FuncType::params() const -> const ownvec<ValType>& {
  return impl(this)->params;
}

WASM_EXPORT auto FuncType::results() const -> const ownvec<ValType>& {
  return impl(this)->results;
}

WASM_EXPORT auto ExternType::func() -> FuncType* {
  return kind() == ExternKind::FUNC
             ? seal<FuncType>(static_cast<FuncTypeImpl*>(impl(this)))
             : nullptr;
}

WASM_EXPORT auto ExternType::func() const -> const FuncType* {
  return kind() == ExternKind::FUNC
             ? seal<FuncType>(static_cast<const FuncTypeImpl*>(impl(this)))
             : nullptr;
}

// Global Types

struct GlobalTypeImpl : ExternTypeImpl {
  own<ValType> content;
  Mutability mutability;

  GlobalTypeImpl(own<ValType>& content, Mutability mutability)
      : ExternTypeImpl(ExternKind::GLOBAL),
        content(std::move(content)),
        mutability(mutability) {}

  ~GlobalTypeImpl() override = default;
};

template <>
struct implement<GlobalType> {
  using type = GlobalTypeImpl;
};

void GlobalType::destroy() { delete impl(this); }

auto GlobalType::make(own<ValType>&& content, Mutability mutability)
    -> own<GlobalType> {
  return content ? own<GlobalType>(seal<GlobalType>(
                       new (std::nothrow) GlobalTypeImpl(content, mutability)))
                 : own<GlobalType>();
}

auto GlobalType::copy() const -> own<GlobalType> {
  return make(content()->copy(), mutability());
}

auto GlobalType::content() const -> const ValType* {
  return impl(this)->content.get();
}

auto GlobalType::mutability() const -> Mutability {
  return impl(this)->mutability;
}

auto ExternType::global() -> GlobalType* {
  return kind() == ExternKind::GLOBAL
             ? seal<GlobalType>(static_cast<GlobalTypeImpl*>(impl(this)))
             : nullptr;
}

auto ExternType::global() const -> const GlobalType* {
  return kind() == ExternKind::GLOBAL
             ? seal<GlobalType>(static_cast<const GlobalTypeImpl*>(impl(this)))
             : nullptr;
}

// Table Types

struct TableTypeImpl : ExternTypeImpl {
  own<ValType> element;
  Limits limits;

  TableTypeImpl(own<ValType>& element, Limits limits)
      : ExternTypeImpl(ExternKind::TABLE),
        element(std::move(element)),
        limits(limits) {}

  ~TableTypeImpl() override = default;
};

template <>
struct implement<TableType> {
  using type = TableTypeImpl;
};

WASM_EXPORT void TableType::destroy() { delete impl(this); }

WASM_EXPORT auto TableType::make(own<ValType>&& element, Limits limits)
    -> own<TableType> {
  return element ? own<TableType>(seal<TableType>(
                       new (std::nothrow) TableTypeImpl(element, limits)))
                 : own<TableType>();
}

WASM_EXPORT auto TableType::copy() const -> own<TableType> {
  return make(element()->copy(), limits());
}

WASM_EXPORT auto TableType::element() const -> const ValType* {
  return impl(this)->element.get();
}

WASM_EXPORT auto TableType::limits() const -> const Limits& {
  return impl(this)->limits;
}

WASM_EXPORT auto ExternType::table() -> TableType* {
  return kind() == ExternKind::TABLE
             ? seal<TableType>(static_cast<TableTypeImpl*>(impl(this)))
             : nullptr;
}

WASM_EXPORT auto ExternType::table() const -> const TableType* {
  return kind() == ExternKind::TABLE
             ? seal<TableType>(static_cast<const TableTypeImpl*>(impl(this)))
             : nullptr;
}

// Memory Types

struct MemoryTypeImpl : ExternTypeImpl {
  Limits limits;

  explicit MemoryTypeImpl(Limits limits)
      : ExternTypeImpl(ExternKind::MEMORY), limits(limits) {}

  ~MemoryTypeImpl() override = default;
};

template <>
struct implement<MemoryType> {
  using type = MemoryTypeImpl;
};

WASM_EXPORT auto MemoryType::make(Limits limits) -> own<MemoryType> {
  return own<MemoryType>(
      seal<MemoryType>(new (std::nothrow) MemoryTypeImpl(limits)));
}

void MemoryType::destroy() { delete impl(this); }

WASM_EXPORT auto MemoryType::copy() const -> own<MemoryType> {
  return MemoryType::make(limits());
}

WASM_EXPORT auto MemoryType::limits() const -> const Limits& {
  return impl(this)->limits;
}

WASM_EXPORT auto ExternType::memory() -> MemoryType* {
  return kind() == ExternKind::MEMORY
             ? seal<MemoryType>(static_cast<MemoryTypeImpl*>(impl(this)))
             : nullptr;
}

WASM_EXPORT auto ExternType::memory() const -> const MemoryType* {
  return kind() == ExternKind::MEMORY
             ? seal<MemoryType>(static_cast<const MemoryTypeImpl*>(impl(this)))
             : nullptr;
}

// Import Types

struct ImportTypeImpl {
  Name module;
  Name name;
  own<ExternType> type;

  ImportTypeImpl(Name& module, Name& name, own<ExternType>& type)
      : module(std::move(module)),
        name(std::move(name)),
        type(std::move(type)) {}
};

template <>
struct implement<ImportType> {
  using type = ImportTypeImpl;
};

WASM_EXPORT void ImportType::destroy() { delete impl(this); }

WASM_EXPORT auto ImportType::make(Name&& module, Name&& name,
                                  own<ExternType>&& type) -> own<ImportType> {
  return module && name && type
             ? own<ImportType>(seal<ImportType>(
                   new (std::nothrow) ImportTypeImpl(module, name, type)))
             : own<ImportType>();
}

WASM_EXPORT auto ImportType::copy() const -> own<ImportType> {
  return make(module().copy(), name().copy(), type()->copy());
}

WASM_EXPORT auto ImportType::module() const -> const Name& {
  return impl(this)->module;
}

WASM_EXPORT auto ImportType::name() const -> const Name& {
  return impl(this)->name;
}

WASM_EXPORT auto ImportType::type() const -> const ExternType* {
  return impl(this)->type.get();
}

// Export Types

struct ExportTypeImpl {
  Name name;
  own<ExternType> type;

  ExportTypeImpl(Name& name, own<ExternType>& type)
      : name(std::move(name)), type(std::move(type)) {}
};

template <>
struct implement<ExportType> {
  using type = ExportTypeImpl;
};

WASM_EXPORT void ExportType::destroy() { delete impl(this); }

WASM_EXPORT auto ExportType::make(Name&& name, own<ExternType>&& type)
    -> own<ExportType> {
  return name && type ? own<ExportType>(seal<ExportType>(
                            new (std::nothrow) ExportTypeImpl(name, type)))
                      : own<ExportType>();
}

WASM_EXPORT auto ExportType::copy() const -> own<ExportType> {
  return make(name().copy(), type()->copy());
}

WASM_EXPORT auto ExportType::name() const -> const Name& {
  return impl(this)->name;
}

WASM_EXPORT auto ExportType::type() const -> const ExternType* {
  return impl(this)->type.get();
}

i::DirectHandle<i::String> VecToString(i::Isolate* isolate,
                                       const vec<byte_t>& chars) {
  size_t length = chars.size();
  // Some, but not all, {chars} vectors we get here are null-terminated,
  // so let's be robust to that.
  if (length > 0 && chars[length - 1] == 0) length--;
  return isolate->factory()
      ->NewStringFromUtf8({chars.get(), length})
      .ToHandleChecked();
}

// References

template <class Ref, class JSType>
class RefImpl {
 public:
  static own<Ref> make(StoreImpl* store, i::DirectHandle<JSType> obj) {
    RefImpl* self = new (std::nothrow) RefImpl();
    if (!self) return nullptr;
    self->store_ = store;
    v8::Isolate::Scope isolate_scope(store->isolate());
    self->val_ = store->i_isolate()->global_handles()->Create(*obj);
    return make_own(seal<Ref>(self));
  }

  ~RefImpl() { i::GlobalHandles::Destroy(location()); }

  own<Ref> copy() const {
    v8::Isolate::Scope isolate_scope(store()->isolate());
    return make(store(), v8_object());
  }

  StoreImpl* store() const { return store_; }

  i::Isolate* isolate() const { return store()->i_isolate(); }

  i::Handle<JSType> v8_object() const {
    return i::Cast<JSType>(val_);
  }

  void* get_host_info() const {
    v8::Isolate::Scope isolate_scope(store()->isolate());
    return store()->GetHostInfo(v8_object());
  }

  void set_host_info(void* info, void (*finalizer)(void*)) {
    v8::Isolate::Scope isolate_scope(store()->isolate());
    store()->SetHostInfo(v8_object(), info, finalizer);
  }

 private:
  RefImpl() = default;

  i::Address* location() const {
    return reinterpret_cast<i::Address*>(val_.address());
  }

  i::Handle<i::JSReceiver> val_;
  StoreImpl* store_;
};

template <>
struct implement<Ref> {
  using type = RefImpl<Ref, i::JSReceiver>;
};

WASM_EXPORT void Ref::destroy() { delete impl(this); }

WASM_EXPORT auto Ref::copy() const -> own<Ref> { return impl(this)->copy(); }

WASM_EXPORT auto Ref::same(const Ref* that) const -> bool {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::HandleScope handle_scope(isolate);
  return i::Object::SameValue(*impl(this)->v8_object(),
                              *impl(that)->v8_object());
}

WASM_EXPORT auto Ref::get_host_info() const -> void* {
  return impl(this)->get_host_info();
}

WASM_EXPORT void Ref::set_host_info(void* info, void (*finalizer)(void*)) {
  impl(this)->set_host_info(info, finalizer);
}

///////////////////////////////////////////////////////////////////////////////
// Runtime Objects

// Frames

namespace {

struct FrameImpl {
  FrameImpl(own<Instance>&& instance, uint32_t func_index, size_t func_offset,
            size_t module_offset)
      : instance(std::move(instance)),
        func_index(func_index),
        func_offset(func_offset),
        module_offset(module_offset) {}

  own<Instance> instance;
  uint32_t func_index;
  size_t func_offset;
  size_t module_offset;
};

}  // namespace

template <>
struct implement<Frame> {
  using type = FrameImpl;
};

WASM_EXPORT void Frame::destroy() { delete impl(this); }

WASM_EXPORT own<Frame> Frame::copy() const {
  auto self = impl(this);
  return own<Frame>(seal<Frame>(
      new (std::nothrow) FrameImpl(self->instance->copy(), self->func_index,
                                   self->func_offset, self->module_offset)));
}

WASM_EXPORT Instance* Frame::instance() const {
  return impl(this)->instance.get();
}

WASM_EXPORT uint32_t Frame::func_index() const {
  return impl(this)->func_index;
}

WASM_EXPORT size_t Frame::func_offset() const {
  return impl(this)->func_offset;
}

WASM_EXPORT size_t Frame::module_offset() const {
  return impl(this)->module_offset;
}

// Traps

template <>
struct implement<Trap> {
  using type = RefImpl<Trap, i::JSReceiver>;
};

WASM_EXPORT void Trap::destroy() { delete impl(this); }

WASM_EXPORT auto Trap::copy() const -> own<Trap> { return impl(this)->copy(); }

WASM_EXPORT auto Trap::make(Store* store_abs, const Message& message)
    -> own<Trap> {
  auto store = impl(store_abs);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope handle_scope(isolate);
  i::DirectHandle<i::String> string = VecToString(isolate, message);
  i::DirectHandle<i::JSObject> exception =
      isolate->factory()->NewError(isolate->error_function(), string);
  i::JSObject::AddProperty(isolate, exception,
                           isolate->factory()->wasm_uncatchable_symbol(),
                           isolate->factory()->true_value(), i::NONE);
  return implement<Trap>::type::make(store, exception);
}

WASM_EXPORT auto Trap::message() const -> Message {
  auto isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::HandleScope handle_scope(isolate);

  i::DirectHandle<i::JSMessageObject> message =
      isolate->CreateMessage(impl(this)->v8_object(), nullptr);
  i::Handle<i::String> result = i::MessageHandler::GetMessage(isolate, message);
  result = i::String::Flatten(isolate, result);  // For performance.
  size_t length = 0;
  std::unique_ptr<char[]> utf8 = result->ToCString(&length);
  return vec<byte_t>::adopt(length, utf8.release());
}

namespace {

own<Instance> GetInstance(StoreImpl* store,
                          i::DirectHandle<i::WasmInstanceObject> instance);

own<Frame> CreateFrameFromInternal(i::DirectHandle<i::FixedArray> frames,
                                   int index, i::Isolate* isolate,
                                   StoreImpl* store) {
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  i::DirectHandle<i::CallSiteInfo> frame(
      i::Cast<i::CallSiteInfo>(frames->get(index)), isolate);
  i::DirectHandle<i::WasmInstanceObject> instance(frame->GetWasmInstance(),
                                                  isolate);
  uint32_t func_index = frame->GetWasmFunctionIndex();
  size_t module_offset = i::CallSiteInfo::GetSourcePosition(frame);
  size_t func_offset = module_offset - i::wasm::GetWasmFunctionOffset(
                                           instance->module(), func_index);
  return own<Frame>(seal<Frame>(new (std::nothrow) FrameImpl(
      GetInstance(store, instance), func_index, func_offset, module_offset)));
}

}  // namespace

own<Frame> Trap::origin() const {
  i::Isolate* isolate = impl(this)->isolate();
  PtrComprCageAccessScope ptr_compr_cage_access_scope(impl(this)->isolate());
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::HandleScope handle_scope(isolate);

  i::DirectHandle<i::FixedArray> frames =
      isolate->GetSimpleStackTrace(impl(this)->v8_object());
  if (frames->length() == 0) {
    return own<Frame>();
  }
  return CreateFrameFromInternal(frames, 0, isolate, impl(this)->store());
}

ownvec<Frame> Trap::trace() const {
  i::Isolate* isolate = impl(this)->isolate();
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::HandleScope handle_scope(isolate);

  i::DirectHandle<i::FixedArray> frames =
      isolate->GetSimpleStackTrace(impl(this)->v8_object());
  int num_frames = frames->length();
  // {num_frames} can be 0; the code below can handle that case.
  ownvec<Frame> result = ownvec<Frame>::make_uninitialized(num_frames);
  for (int i = 0; i < num_frames; i++) {
    result[i] =
        CreateFrameFromInternal(frames, i, isolate, impl(this)->store());
  }
  return result;
}

// Foreign Objects

template <>
struct implement<Foreign> {
  using type = RefImpl<Foreign, i::JSReceiver>;
};

WASM_EXPORT void Foreign::destroy() { delete impl(this); }

WASM_EXPORT auto Foreign::copy() const -> own<Foreign> {
  return impl(this)->copy();
}

WASM_EXPORT auto Foreign::make(Store* store_abs) -> own<Foreign> {
  StoreImpl* store = impl(store_abs);
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::Isolate* isolate = store->i_isolate();
  i::HandleScope handle_scope(isolate);

  i::DirectHandle<i::JSObject> obj =
      isolate->factory()->NewJSObject(isolate->object_function());
  return implement<Foreign>::type::make(store, obj);
}

// Modules

template <>
struct implement<Module> {
  using type = RefImpl<Module, i::WasmModuleObject>;
};

WASM_EXPORT void Module::destroy() { delete impl(this); }

WASM_EXPORT auto Module::copy() const -> own<Module> {
  return impl(this)->copy();
}

WASM_EXPORT auto Module::validate(Store* store_abs, const vec<byte_t>& binary)
    -> bool {
  i::Isolate* isolate = impl(store_abs)->i_isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  i::HandleScope scope(isolate);
  v8::base::Vector<const uint8_t> bytes = v8::base::VectorOf(
      reinterpret_cast<const uint8_t*>(binary.get()), binary.size());
  i::wasm::WasmEnabledFeatures features =
      i::wasm::WasmEnabledFeatures::FromIsolate(isolate);
  i::wasm::CompileTimeImports imports;
  return i::wasm::GetWasmEngine()->SyncValidate(isolate, features,
                                                std::move(imports), bytes);
}

WASM_EXPORT auto Module::make(Store* store_abs, const vec<byte_t>& binary)
    -> own<Module> {
  StoreImpl* store = impl(store_abs);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope scope(isolate);
  CheckAndHandleInterrupts(isolate);
  v8::base::OwnedVector<const uint8_t> bytes = v8::base::OwnedCopyOf(
      reinterpret_cast<const uint8_t*>(binary.get()), binary.size());
  i::wasm::WasmEnabledFeatures features =
      i::wasm::WasmEnabledFeatures::FromIsolate(isolate);
  i::wasm::CompileTimeImports imports;
  i::wasm::ErrorThrower thrower(isolate, "ignored");
  i::DirectHandle<i::WasmModuleObject> module;
  if (!i::wasm::GetWasmEngine()
           ->SyncCompile(isolate, features, std::move(imports), &thrower,
                         std::move(bytes))
           .ToHandle(&module)) {
    thrower.Reset();  // The API provides no way to expose the error.
    return nullptr;
  }
  return implement<Module>::type::make(store, module);
}

WASM_EXPORT auto Module::imports() const -> ownvec<ImportType> {
  const i::wasm::NativeModule* native_module =
      impl(this)->v8_object()->native_module();
  const i::wasm::WasmModule* module = native_module->module();
  const v8::base::Vector<const uint8_t> wire_bytes =
      native_module->wire_bytes();
  const std::vector<i::wasm::WasmImport>& import_table = module->import_table;
  size_t size = import_table.size();
  ownvec<ImportType> imports = ownvec<ImportType>::make_uninitialized(size);
  for (uint32_t i = 0; i < size; i++) {
    const i::wasm::WasmImport& imp = import_table[i];
    Name module_name = GetNameFromWireBytes(imp.module_name, wire_bytes);
    Name name = GetNameFromWireBytes(imp.field_name, wire_bytes);
    own<ExternType> type = GetImportExportType(module, imp.kind, imp.index);
    imports[i] = ImportType::make(std::move(module_name), std::move(name),
                                  std::move(type));
  }
  return imports;
}

ownvec<ExportType> ExportsImpl(
    i::DirectHandle<i::WasmModuleObject> module_obj) {
  const i::wasm::NativeModule* native_module = module_obj->native_module();
  const i::wasm::WasmModule* module = native_module->module();
  const v8::base::Vector<const uint8_t> wire_bytes =
      native_module->wire_bytes();
  const std::vector<i::wasm::WasmExport>& export_table = module->export_table;
  size_t size = export_table.size();
  ownvec<ExportType> exports = ownvec<ExportType>::make_uninitialized(size);
  for (uint32_t i = 0; i < size; i++) {
    const i::wasm::WasmExport& exp = export_table[i];
    Name name = GetNameFromWireBytes(exp.name, wire_bytes);
    own<ExternType> type = GetImportExportType(module, exp.kind, exp.index);
    exports[i] = ExportType::make(std::move(name), std::move(type));
  }
  return exports;
}

WASM_EXPORT auto Module::exports() const -> ownvec<ExportType> {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  return ExportsImpl(impl(this)->v8_object());
}

// We tier up all functions to TurboFan, and then serialize all TurboFan code.
// If no TurboFan code existed before calling this function, then the call to
// {serialize} may take a long time.
WASM_EXPORT auto Module::serialize() const -> vec<byte_t> {
  i::Isolate* isolate = impl(this)->isolate();
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::wasm::NativeModule* native_module =
      impl(this)->v8_object()->native_module();
  native_module->compilation_state()->TierUpAllFunctions();
  v8::base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  size_t binary_size = wire_bytes.size();
  i::wasm::WasmSerializer serializer(native_module);
  size_t serial_size = serializer.GetSerializedNativeModuleSize();
  size_t size_size = i::wasm::LEBHelper::sizeof_u64v(binary_size);
  vec<byte_t> buffer =
      vec<byte_t>::make_uninitialized(size_size + binary_size + serial_size);
  byte_t* ptr = buffer.get();
  i::wasm::LEBHelper::write_u64v(reinterpret_cast<uint8_t**>(&ptr),
                                 binary_size);
  std::memcpy(ptr, wire_bytes.begin(), binary_size);
  ptr += binary_size;
  if (!serializer.SerializeNativeModule(
          {reinterpret_cast<uint8_t*>(ptr), serial_size})) {
    // Serialization fails if no TurboFan code is present. This may happen
    // because the module does not have any functions, or because another thread
    // modifies the {NativeModule} concurrently. In this case, the serialized
    // module just contains the wire bytes.
    buffer = vec<byte_t>::make_uninitialized(size_size + binary_size);
    byte_t* pointer = buffer.get();
    i::wasm::LEBHelper::write_u64v(reinterpret_cast<uint8_t**>(&pointer),
                                   binary_size);
    std::memcpy(pointer, wire_bytes.begin(), binary_size);
  }
  return buffer;
}

WASM_EXPORT auto Module::deserialize(Store* store_abs,
                                     const vec<byte_t>& serialized)
    -> own<Module> {
  StoreImpl* store = impl(store_abs);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope handle_scope(isolate);
  const byte_t* ptr = serialized.get();
  uint64_t binary_size = ReadLebU64(&ptr);
  ptrdiff_t size_size = ptr - serialized.get();
  size_t serial_size = serialized.size() - size_size - binary_size;
  i::DirectHandle<i::WasmModuleObject> module_obj;
  if (serial_size > 0) {
    size_t data_size = static_cast<size_t>(binary_size);
    i::wasm::CompileTimeImports compile_imports{};
    if (!i::wasm::DeserializeNativeModule(
             isolate,
             {reinterpret_cast<const uint8_t*>(ptr + data_size), serial_size},
             {reinterpret_cast<const uint8_t*>(ptr), data_size},
             compile_imports, {})
             .ToHandle(&module_obj)) {
      // We were given a serialized module, but failed to deserialize. Report
      // this as an error.
      return nullptr;
    }
  } else {
    // No serialized module was given. This is fine, just create a module from
    // scratch.
    vec<byte_t> binary = vec<byte_t>::make_uninitialized(binary_size);
    std::memcpy(binary.get(), ptr, binary_size);
    return make(store_abs, binary);
  }
  return implement<Module>::type::make(store, module_obj);
}

// TODO(v8): do better when V8 can do better.
template <>
struct implement<Shared<Module>> {
  using type = vec<byte_t>;
};

WASM_EXPORT void Shared<Module>::destroy() { delete impl(this); }

WASM_EXPORT auto Module::share() const -> own<Shared<Module>> {
  auto shared = seal<Shared<Module>>(new vec<byte_t>(serialize()));
  return make_own(shared);
}

WASM_EXPORT auto Module::obtain(Store* store, const Shared<Module>* shared)
    -> own<Module> {
  return Module::deserialize(store, *impl(shared));
}

// Externals

template <>
struct implement<Extern> {
  using type = RefImpl<Extern, i::JSReceiver>;
};

WASM_EXPORT void Extern::destroy() { delete impl(this); }

WASM_EXPORT auto Extern::copy() const -> own<Extern> {
  return impl(this)->copy();
}

WASM_EXPORT auto Extern::kind() const -> ExternKind {
  i::Isolate* isolate = impl(this)->isolate();
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));

  i::DirectHandle<i::JSReceiver> obj = impl(this)->v8_object();
  if (i::WasmExternalFunction::IsWasmExternalFunction(*obj)) {
    return wasm::ExternKind::FUNC;
  }
  if (IsWasmGlobalObject(*obj)) return wasm::ExternKind::GLOBAL;
  if (IsWasmTableObject(*obj)) return wasm::ExternKind::TABLE;
  if (IsWasmMemoryObject(*obj)) return wasm::ExternKind::MEMORY;
  UNREACHABLE();
}

WASM_EXPORT auto Extern::type() const -> own<ExternType> {
  switch (kind()) {
    case ExternKind::FUNC:
      return func()->type();
    case ExternKind::GLOBAL:
      return global()->type();
    case ExternKind::TABLE:
      return table()->type();
    case ExternKind::MEMORY:
      return memory()->type();
  }
}

WASM_EXPORT auto Extern::func() -> Func* {
  return kind() == ExternKind::FUNC ? static_cast<Func*>(this) : nullptr;
}

WASM_EXPORT auto Extern::global() -> Global* {
  return kind() == ExternKind::GLOBAL ? static_cast<Global*>(this) : nullptr;
}

WASM_EXPORT auto Extern::table() -> Table* {
  return kind() == ExternKind::TABLE ? static_cast<Table*>(this) : nullptr;
}

WASM_EXPORT auto Extern::memory() -> Memory* {
  return kind() == ExternKind::MEMORY ? static_cast<Memory*>(this) : nullptr;
}

WASM_EXPORT auto Extern::func() const -> const Func* {
  return kind() == ExternKind::FUNC ? static_cast<const Func*>(this) : nullptr;
}

WASM_EXPORT auto Extern::global() const -> const Global* {
  return kind() == ExternKind::GLOBAL ? static_cast<const Global*>(this)
                                      : nullptr;
}

WASM_EXPORT auto Extern::table() const -> const Table* {
  return kind() == ExternKind::TABLE ? static_cast<const Table*>(this)
                                     : nullptr;
}

WASM_EXPORT auto Extern::memory() const -> const Memory* {
  return kind() == ExternKind::MEMORY ? static_cast<const Memory*>(this)
                                      : nullptr;
}

auto extern_to_v8(const Extern* ex) -> i::Handle<i::JSReceiver> {
  return impl(ex)->v8_object();
}

// Function Instances

template <>
struct implement<Func> {
  using type = RefImpl<Func, i::JSFunction>;
};

WASM_EXPORT void Func::destroy() { delete impl(this); }

WASM_EXPORT auto Func::copy() const -> own<Func> { return impl(this)->copy(); }

struct FuncData {
  static constexpr i::ExternalPointerTag kManagedTag = i::kWasmFuncDataTag;

  Store* store;
  own<FuncType> type;
  enum Kind { kCallback, kCallbackWithEnv } kind;
  union {
    Func::callback callback;
    Func::callback_with_env callback_with_env;
  };
  void (*finalizer)(void*);
  void* env;

  FuncData(Store* store, const FuncType* type, Kind kind)
      : store(store),
        type(type->copy()),
        kind(kind),
        finalizer(nullptr),
        env(nullptr) {}

  ~FuncData() {
    if (finalizer) (*finalizer)(env);
  }

  static i::Address v8_callback(i::Address host_data_foreign, i::Address argv);
};

namespace {

class SignatureHelper : public i::AllStatic {
 public:
  static const i::wasm::CanonicalTypeIndex Canonicalize(FuncType* type) {
    std::vector<i::wasm::ValueType> types;
    types.reserve(type->results().size() + type->params().size());

    // TODO(jkummerow): Consider making vec<> range-based for-iterable.
    for (size_t i = 0; i < type->results().size(); i++) {
      types.push_back(WasmValKindToV8(type->results()[i]->kind()));
    }
    for (size_t i = 0; i < type->params().size(); i++) {
      types.push_back(WasmValKindToV8(type->params()[i]->kind()));
    }

    i::wasm::FunctionSig non_canonical_sig{type->results().size(),
                                           type->params().size(), types.data()};
    return i::wasm::GetTypeCanonicalizer()->AddRecursiveGroup(
        &non_canonical_sig);
  }

  static own<FuncType> FromV8Sig(const i::wasm::CanonicalSig* sig) {
    int result_arity = static_cast<int>(sig->return_count());
    int param_arity = static_cast<int>(sig->parameter_count());
    ownvec<ValType> results = ownvec<ValType>::make_uninitialized(result_arity);
    ownvec<ValType> params = ownvec<ValType>::make_uninitialized(param_arity);

    for (int i = 0; i < result_arity; ++i) {
      results[i] = ValType::make(V8ValueTypeToWasm(sig->GetReturn(i)));
    }
    for (int i = 0; i < param_arity; ++i) {
      params[i] = ValType::make(V8ValueTypeToWasm(sig->GetParam(i)));
    }
    return FuncType::make(std::move(params), std::move(results));
  }

  static const i::wasm::CanonicalSig* GetSig(
      i::DirectHandle<i::JSFunction> function) {
    return i::Cast<i::WasmCapiFunction>(*function)->sig();
  }

#if V8_ENABLE_SANDBOX
  // Wraps {FuncType} so it has the same interface as {v8::internal::Signature}.
  struct FuncTypeAdapter {
    const FuncType* type = nullptr;
    size_t parameter_count() const { return type->params().size(); }
    size_t return_count() const { return type->results().size(); }
    i::wasm::ValueType GetParam(size_t i) const {
      return WasmValKindToV8(type->params()[i]->kind());
    }
    i::wasm::ValueType GetReturn(size_t i) const {
      return WasmValKindToV8(type->results()[i]->kind());
    }
  };
  static uint64_t Hash(FuncType* type) {
    FuncTypeAdapter adapter{type};
    return i::wasm::SignatureHasher::Hash(&adapter);
  }
#endif
};

auto make_func(Store* store_abs, std::shared_ptr<FuncData> data) -> own<Func> {
  auto store = impl(store_abs);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope handle_scope(isolate);
  CheckAndHandleInterrupts(isolate);
  i::DirectHandle<i::Managed<FuncData>> embedder_data =
      i::Managed<FuncData>::From(isolate, sizeof(FuncData), data);
  i::wasm::CanonicalTypeIndex sig_index =
      SignatureHelper::Canonicalize(data->type.get());
  const i::wasm::CanonicalSig* sig =
      i::wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_index);
  i::DirectHandle<i::WasmCapiFunction> function = i::WasmCapiFunction::New(
      isolate, reinterpret_cast<i::Address>(&FuncData::v8_callback),
      embedder_data, sig_index, sig);
  i::Cast<i::WasmImportData>(
      function->shared()->wasm_capi_function_data()->internal()->implicit_arg())
      ->set_callable(*function);
  auto func = implement<Func>::type::make(store, function);
  return func;
}

}  // namespace

WASM_EXPORT auto Func::make(Store* store, const FuncType* type,
                            Func::callback callback) -> own<Func> {
  auto data = std::make_shared<FuncData>(store, type, FuncData::kCallback);
  data->callback = callback;
  return make_func(store, data);
}

WASM_EXPORT auto Func::make(Store* store, const FuncType* type,
                            callback_with_env callback, void* env,
                            void (*finalizer)(void*)) -> own<Func> {
  auto data =
      std::make_shared<FuncData>(store, type, FuncData::kCallbackWithEnv);
  data->callback_with_env = callback;
  data->env = env;
  data->finalizer = finalizer;
  return make_func(store, data);
}

WASM_EXPORT auto Func::type() const -> own<FuncType> {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  i::DirectHandle<i::JSFunction> func = impl(this)->v8_object();
  if (i::WasmCapiFunction::IsWasmCapiFunction(*func)) {
    return SignatureHelper::FromV8Sig(SignatureHelper::GetSig(func));
  }
  DCHECK(i::WasmExportedFunction::IsWasmExportedFunction(*func));
  auto function = i::Cast<i::WasmExportedFunction>(func);
  auto data = function->shared()->wasm_exported_function_data();
  return FunctionSigToFuncType(
      data->instance_data()->module()->functions[data->function_index()].sig);
}

WASM_EXPORT auto Func::param_arity() const -> size_t {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  i::DirectHandle<i::JSFunction> func = impl(this)->v8_object();
  if (i::WasmCapiFunction::IsWasmCapiFunction(*func)) {
    return SignatureHelper::GetSig(func)->parameter_count();
  }
  DCHECK(i::WasmExportedFunction::IsWasmExportedFunction(*func));
  auto function = i::Cast<i::WasmExportedFunction>(func);
  auto data = function->shared()->wasm_exported_function_data();
  const i::wasm::FunctionSig* sig =
      data->instance_data()->module()->functions[data->function_index()].sig;
  return sig->parameter_count();
}

WASM_EXPORT auto Func::result_arity() const -> size_t {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  i::DirectHandle<i::JSFunction> func = impl(this)->v8_object();
  if (i::WasmCapiFunction::IsWasmCapiFunction(*func)) {
    return SignatureHelper::GetSig(func)->return_count();
  }
  DCHECK(i::WasmExportedFunction::IsWasmExportedFunction(*func));
  auto function = i::Cast<i::WasmExportedFunction>(func);
  auto data = function->shared()->wasm_exported_function_data();
  const i::wasm::FunctionSig* sig =
      data->instance_data()->module()->functions[data->function_index()].sig;
  return sig->return_count();
}

namespace {

own<Ref> V8RefValueToWasm(StoreImpl* store, i::DirectHandle<i::Object> value) {
  if (IsNull(*value, store->i_isolate())) return nullptr;
  return implement<Ref>::type::make(store, i::Cast<i::JSReceiver>(value));
}

i::Handle<i::Object> WasmRefToV8(i::Isolate* isolate, const Ref* ref) {
  if (ref == nullptr) return isolate->factory()->null_value();
  return impl(ref)->v8_object();
}

void PrepareFunctionData(
    i::Isolate* isolate,
    i::DirectHandle<i::WasmExportedFunctionData> function_data,
    const i::wasm::CanonicalSig* sig) {
  // If the data is already populated, return immediately.
  // TODO(saelo): We need to use full pointer comparison here while not all Code
  // objects have migrated into trusted space.
  static_assert(!i::kAllCodeObjectsLiveInTrustedSpace);
  if (!function_data->c_wrapper_code(isolate).SafeEquals(
          *BUILTIN_CODE(isolate, Illegal))) {
    return;
  }
  // Compile wrapper code.
  i::DirectHandle<i::Code> wrapper_code =
      i::compiler::CompileCWasmEntry(isolate, sig);
  function_data->set_c_wrapper_code(*wrapper_code);
  // Compute packed args size.
  function_data->set_packed_args_size(
      i::wasm::CWasmArgumentsPacker::TotalSize(sig));
}

void PushArgs(const i::wasm::CanonicalSig* sig, const vec<Val>& args,
              i::wasm::CWasmArgumentsPacker* packer, StoreImpl* store) {
  for (size_t i = 0; i < sig->parameter_count(); i++) {
    i::wasm::CanonicalValueType type = sig->GetParam(i);
    switch (type.kind()) {
      case i::wasm::kI32:
        packer->Push(args[i].i32());
        break;
      case i::wasm::kI64:
        packer->Push(args[i].i64());
        break;
      case i::wasm::kF32:
        packer->Push(args[i].f32());
        break;
      case i::wasm::kF64:
        packer->Push(args[i].f64());
        break;
      case i::wasm::kRef:
      case i::wasm::kRefNull:
        // TODO(14034): Make sure this works for all heap types.
        packer->Push((*WasmRefToV8(store->i_isolate(), args[i].ref())).ptr());
        break;
      case i::wasm::kS128:
        // TODO(14034): Implement.
        UNIMPLEMENTED();
      case i::wasm::kRtt:
      case i::wasm::kI8:
      case i::wasm::kI16:
      case i::wasm::kF16:
      case i::wasm::kVoid:
      case i::wasm::kTop:
      case i::wasm::kBottom:
        UNREACHABLE();
    }
  }
}

void PopArgs(const i::wasm::CanonicalSig* sig, vec<Val>& results,
             i::wasm::CWasmArgumentsPacker* packer, StoreImpl* store) {
  packer->Reset();
  for (size_t i = 0; i < sig->return_count(); i++) {
    i::wasm::CanonicalValueType type = sig->GetReturn(i);
    switch (type.kind()) {
      case i::wasm::kI32:
        results[i] = Val(packer->Pop<int32_t>());
        break;
      case i::wasm::kI64:
        results[i] = Val(packer->Pop<int64_t>());
        break;
      case i::wasm::kF32:
        results[i] = Val(packer->Pop<float>());
        break;
      case i::wasm::kF64:
        results[i] = Val(packer->Pop<double>());
        break;
      case i::wasm::kRef:
      case i::wasm::kRefNull: {
        // TODO(14034): Make sure this works for all heap types.
        i::Address raw = packer->Pop<i::Address>();
        i::DirectHandle<i::Object> obj(i::Tagged<i::Object>(raw),
                                       store->i_isolate());
        results[i] = Val(V8RefValueToWasm(store, obj));
        break;
      }
      case i::wasm::kS128:
        // TODO(14034): Implement.
        UNIMPLEMENTED();
      case i::wasm::kRtt:
      case i::wasm::kI8:
      case i::wasm::kI16:
      case i::wasm::kF16:
      case i::wasm::kVoid:
      case i::wasm::kTop:
      case i::wasm::kBottom:
        UNREACHABLE();
    }
  }
}

own<Trap> CallWasmCapiFunction(i::Tagged<i::WasmCapiFunctionData> data,
                               const vec<Val>& args, vec<Val>& results) {
  FuncData* func_data =
      i::Cast<i::Managed<FuncData>>(data->embedder_data())->raw();
  if (func_data->kind == FuncData::kCallback) {
    return (func_data->callback)(args, results);
  }
  DCHECK(func_data->kind == FuncData::kCallbackWithEnv);
  return (func_data->callback_with_env)(func_data->env, args, results);
}

i::DirectHandle<i::JSReceiver> GetProperException(
    i::Isolate* isolate, i::Handle<i::Object> maybe_exception) {
  if (IsJSReceiver(*maybe_exception)) {
    return i::Cast<i::JSReceiver>(maybe_exception);
  }
  if (v8::internal::IsTerminationException(*maybe_exception)) {
    i::DirectHandle<i::String> string =
        isolate->factory()->NewStringFromAsciiChecked("TerminationException");
    return isolate->factory()->NewError(isolate->error_function(), string);
  }
  i::MaybeDirectHandle<i::String> maybe_string =
      i::Object::ToString(isolate, maybe_exception);
  i::DirectHandle<i::String> string = isolate->factory()->empty_string();
  if (!maybe_string.ToHandle(&string)) {
    // If converting the {maybe_exception} to string threw another exception,
    // just give up and leave {string} as the empty string.
    isolate->clear_exception();
  }
  // {NewError} cannot fail when its input is a plain String, so we always
  // get an Error object here.
  return i::Cast<i::JSReceiver>(
      isolate->factory()->NewError(isolate->error_function(), string));
}

}  // namespace

WASM_EXPORT auto Func::call(const vec<Val>& args, vec<Val>& results) const
    -> own<Trap> {
  auto func = impl(this);
  auto store = func->store();
  auto isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope handle_scope(isolate);
  i::Tagged<i::Object> raw_function_data =
      func->v8_object()->shared()->GetTrustedData(isolate);

  // WasmCapiFunctions can be called directly.
  if (IsWasmCapiFunctionData(raw_function_data)) {
    return CallWasmCapiFunction(
        i::Cast<i::WasmCapiFunctionData>(raw_function_data), args, results);
  }

  SBXCHECK(IsWasmExportedFunctionData(raw_function_data));
  i::DirectHandle<i::WasmExportedFunctionData> function_data{
      i::Cast<i::WasmExportedFunctionData>(raw_function_data), isolate};
  i::DirectHandle<i::WasmTrustedInstanceData> instance_data{
      function_data->instance_data(), isolate};
  int function_index = function_data->function_index();
  const i::wasm::WasmModule* module = instance_data->module();
  // Caching {sig} would reduce overhead substantially.
  const i::wasm::CanonicalSig* sig =
      i::wasm::GetTypeCanonicalizer()->LookupFunctionSignature(
          module->canonical_sig_id(
              module->functions[function_index].sig_index));
  PrepareFunctionData(isolate, function_data, sig);
  i::DirectHandle<i::Code> wrapper_code(function_data->c_wrapper_code(isolate),
                                        isolate);
  i::WasmCodePointer call_target = function_data->internal()->call_target();

  i::wasm::CWasmArgumentsPacker packer(function_data->packed_args_size());
  PushArgs(sig, args, &packer, store);

  i::DirectHandle<i::Object> object_ref;
  if (function_index < static_cast<int>(module->num_imported_functions)) {
    object_ref = i::direct_handle(
        instance_data->dispatch_table_for_imports()->implicit_arg(
            function_index),
        isolate);
    if (IsWasmImportData(*object_ref)) {
      i::Tagged<i::JSFunction> jsfunc = i::Cast<i::JSFunction>(
          i::Cast<i::WasmImportData>(*object_ref)->callable());
      i::Tagged<i::Object> data = jsfunc->shared()->GetTrustedData(isolate);
      if (IsWasmCapiFunctionData(data)) {
        return CallWasmCapiFunction(i::Cast<i::WasmCapiFunctionData>(data),
                                    args, results);
      }
      // TODO(jkummerow): Imported and then re-exported JavaScript functions
      // are not supported yet. If we support C-API + JavaScript, we'll need
      // to call those here.
      UNIMPLEMENTED();
    } else {
      // A WasmFunction from another module.
      DCHECK(IsWasmInstanceObject(*object_ref));
    }
  } else {
    // TODO(42204563): Avoid crashing if the instance object is not available.
    CHECK(instance_data->has_instance_object());
    object_ref = direct_handle(instance_data->instance_object(), isolate);
  }

  i::Execution::CallWasm(isolate, wrapper_code, call_target, object_ref,
                         packer.argv());

  if (isolate->has_exception()) {
    i::Handle<i::Object> exception(isolate->exception(), isolate);
    isolate->clear_exception();
    return implement<Trap>::type::make(store,
                                       GetProperException(isolate, exception));
  }

  PopArgs(sig, results, &packer, store);
  return nullptr;
}

i::Address FuncData::v8_callback(i::Address host_data_foreign,
                                 i::Address argv) {
  FuncData* self =
      i::Cast<i::Managed<FuncData>>(i::Tagged<i::Object>(host_data_foreign))
          ->raw();
  StoreImpl* store = impl(self->store);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope scope(isolate);

  isolate->set_context(*v8::Utils::OpenDirectHandle(*store->context()));

  const ownvec<ValType>& param_types = self->type->params();
  const ownvec<ValType>& result_types = self->type->results();

  int num_param_types = static_cast<int>(param_types.size());
  int num_result_types = static_cast<int>(result_types.size());

  auto params = vec<Val>::make_uninitialized(num_param_types);
  auto results = vec<Val>::make_uninitialized(num_result_types);
  i::Address p = argv;
  for (int i = 0; i < num_param_types; ++i) {
    switch (param_types[i]->kind()) {
      case ValKind::I32:
        params[i] = Val(v8::base::ReadUnalignedValue<int32_t>(p));
        p += 4;
        break;
      case ValKind::I64:
        params[i] = Val(v8::base::ReadUnalignedValue<int64_t>(p));
        p += 8;
        break;
      case ValKind::F32:
        params[i] = Val(v8::base::ReadUnalignedValue<float32_t>(p));
        p += 4;
        break;
      case ValKind::F64:
        params[i] = Val(v8::base::ReadUnalignedValue<float64_t>(p));
        p += 8;
        break;
      case ValKind::EXTERNREF:
      case ValKind::FUNCREF: {
        i::Address raw = v8::base::ReadUnalignedValue<i::Address>(p);
        p += sizeof(raw);
        i::DirectHandle<i::Object> obj(i::Tagged<i::Object>(raw), isolate);
        params[i] = Val(V8RefValueToWasm(store, obj));
        break;
      }
    }
  }

  own<Trap> trap;
  if (self->kind == kCallbackWithEnv) {
    trap = self->callback_with_env(self->env, params, results);
  } else {
    trap = self->callback(params, results);
  }

  if (trap) {
    isolate->Throw(*impl(trap.get())->v8_object());
    i::Tagged<i::Object> ex = isolate->exception();
    isolate->clear_exception();
    return ex.ptr();
  }

  p = argv;
  for (int i = 0; i < num_result_types; ++i) {
    switch (result_types[i]->kind()) {
      case ValKind::I32:
        v8::base::WriteUnalignedValue(p, results[i].i32());
        p += 4;
        break;
      case ValKind::I64:
        v8::base::WriteUnalignedValue(p, results[i].i64());
        p += 8;
        break;
      case ValKind::F32:
        v8::base::WriteUnalignedValue(p, results[i].f32());
        p += 4;
        break;
      case ValKind::F64:
        v8::base::WriteUnalignedValue(p, results[i].f64());
        p += 8;
        break;
      case ValKind::EXTERNREF:
      case ValKind::FUNCREF: {
        v8::base::WriteUnalignedValue(
            p, (*WasmRefToV8(isolate, results[i].ref())).ptr());
        p += sizeof(i::Address);
        break;
      }
    }
  }
  return i::kNullAddress;
}

// Global Instances

template <>
struct implement<Global> {
  using type = RefImpl<Global, i::WasmGlobalObject>;
};

WASM_EXPORT void Global::destroy() { delete (this); }

WASM_EXPORT auto Global::copy() const -> own<Global> {
  return impl(this)->copy();
}

WASM_EXPORT auto Global::make(Store* store_abs, const GlobalType* type,
                              const Val& val) -> own<Global> {
  StoreImpl* store = impl(store_abs);
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::Isolate* isolate = store->i_isolate();
  i::HandleScope handle_scope(isolate);
  CheckAndHandleInterrupts(isolate);

  DCHECK_EQ(type->content()->kind(), val.kind());

  i::wasm::ValueType i_type = WasmValKindToV8(type->content()->kind());
  bool is_mutable = (type->mutability() == Mutability::VAR);
  const int32_t offset = 0;
  i::DirectHandle<i::WasmGlobalObject> obj =
      i::WasmGlobalObject::New(
          isolate, i::DirectHandle<i::WasmTrustedInstanceData>(),
          i::MaybeDirectHandle<i::JSArrayBuffer>(),
          i::MaybeDirectHandle<i::FixedArray>(), i_type, offset, is_mutable)
          .ToHandleChecked();

  auto global = implement<Global>::type::make(store, obj);
  assert(global);
  global->set(val);
  return global;
}

WASM_EXPORT auto Global::type() const -> own<GlobalType> {
  i::DirectHandle<i::WasmGlobalObject> v8_global = impl(this)->v8_object();
  ValKind kind = V8ValueTypeToWasm(v8_global->type());
  Mutability mutability =
      v8_global->is_mutable() ? Mutability::VAR : Mutability::CONST;
  return GlobalType::make(ValType::make(kind), mutability);
}

WASM_EXPORT auto Global::get() const -> Val {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  i::DirectHandle<i::WasmGlobalObject> v8_global = impl(this)->v8_object();
  switch (v8_global->type().kind()) {
    case i::wasm::kI32:
      return Val(v8_global->GetI32());
    case i::wasm::kI64:
      return Val(v8_global->GetI64());
    case i::wasm::kF32:
      return Val(v8_global->GetF32());
    case i::wasm::kF64:
      return Val(v8_global->GetF64());
    case i::wasm::kRef:
    case i::wasm::kRefNull: {
      // TODO(14034): Handle types other than funcref and externref if needed.
      StoreImpl* store = impl(this)->store();
      i::HandleScope scope(store->i_isolate());
      i::DirectHandle<i::Object> result = v8_global->GetRef();
      if (IsWasmFuncRef(*result)) {
        result = i::WasmInternalFunction::GetOrCreateExternal(i::direct_handle(
            i::Cast<i::WasmFuncRef>(*result)->internal(store->i_isolate()),
            store->i_isolate()));
      }
      if (IsWasmNull(*result)) {
        result = v8_global->GetIsolate()->factory()->null_value();
      }
      return Val(V8RefValueToWasm(store, result));
    }
    case i::wasm::kS128:
      // TODO(14034): Implement these.
      UNIMPLEMENTED();
    case i::wasm::kRtt:
    case i::wasm::kI8:
    case i::wasm::kI16:
    case i::wasm::kF16:
    case i::wasm::kVoid:
    case i::wasm::kTop:
    case i::wasm::kBottom:
      UNREACHABLE();
  }
}

WASM_EXPORT void Global::set(const Val& val) {
  v8::Isolate::Scope isolate_scope(impl(this)->store()->isolate());
  i::DirectHandle<i::WasmGlobalObject> v8_global = impl(this)->v8_object();
  switch (val.kind()) {
    case ValKind::I32:
      return v8_global->SetI32(val.i32());
    case ValKind::I64:
      return v8_global->SetI64(val.i64());
    case ValKind::F32:
      return v8_global->SetF32(val.f32());
    case ValKind::F64:
      return v8_global->SetF64(val.f64());
    case ValKind::EXTERNREF:
      return v8_global->SetRef(
          WasmRefToV8(impl(this)->store()->i_isolate(), val.ref()));
    case ValKind::FUNCREF: {
      i::Isolate* isolate = impl(this)->store()->i_isolate();
      auto external = WasmRefToV8(impl(this)->store()->i_isolate(), val.ref());
      const char* error_message;
      auto internal = i::wasm::JSToWasmObject(isolate, nullptr, external,
                                              v8_global->type(), &error_message)
                          .ToHandleChecked();
      v8_global->SetRef(internal);
      return;
    }
    default:
      // TODO(wasm+): support new value types
      UNREACHABLE();
  }
}

// Table Instances

template <>
struct implement<Table> {
  using type = RefImpl<Table, i::WasmTableObject>;
};

WASM_EXPORT void Table::destroy() { delete impl(this); }

WASM_EXPORT auto Table::copy() const -> own<Table> {
  return impl(this)->copy();
}

WASM_EXPORT auto Table::make(Store* store_abs, const TableType* type,
                             const Ref* ref) -> own<Table> {
  StoreImpl* store = impl(store_abs);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope scope(isolate);
  CheckAndHandleInterrupts(isolate);

  // Get "element".
  i::wasm::ValueType i_type;
  i::wasm::CanonicalValueType canonical_type;
  switch (type->element()->kind()) {
    case ValKind::FUNCREF:
      i_type = i::wasm::kWasmFuncRef;
      canonical_type = i::wasm::kCanonicalFuncRef;
      break;
    case ValKind::EXTERNREF:
      // See Engine::make().
      i_type = i::wasm::kWasmExternRef;
      canonical_type = i::wasm::kCanonicalExternRef;
      break;
    default:
      UNREACHABLE();
  }

  const Limits& limits = type->limits();
  uint32_t minimum = limits.min;
  if (minimum > i::wasm::max_table_init_entries()) return nullptr;
  uint32_t maximum = limits.max;
  bool has_maximum = false;
  if (maximum != Limits(0).max) {
    has_maximum = true;
    if (maximum < minimum) return nullptr;
    if (maximum > i::wasm::max_table_init_entries()) return nullptr;
  }

  i::DirectHandle<i::WasmTableObject> table_obj = i::WasmTableObject::New(
      isolate, i::DirectHandle<i::WasmTrustedInstanceData>(), i_type,
      canonical_type, minimum, has_maximum, maximum,
      isolate->factory()->null_value(), i::wasm::AddressType::kI32);

  if (ref) {
    i::DirectHandle<i::JSReceiver> init = impl(ref)->v8_object();
    for (uint32_t i = 0; i < minimum; i++) {
      table_obj->Set(isolate, table_obj, i, init);
    }
  }
  return implement<Table>::type::make(store, table_obj);
}

WASM_EXPORT auto Table::type() const -> own<TableType> {
  i::DirectHandle<i::WasmTableObject> table = impl(this)->v8_object();
  uint32_t min = table->current_length();
  // Note: The C-API is not updated for memory64 yet; limits use uint32_t. Thus
  // truncate the actual declared maximum to kMaxUint32.
  uint32_t max = static_cast<uint32_t>(std::min<uint64_t>(
      i::kMaxUInt32, table->maximum_length_u64().value_or(i::kMaxUInt32)));
  ValKind kind;
  switch (table->unsafe_type().heap_representation()) {
    case i::wasm::HeapType::kFunc:
      kind = ValKind::FUNCREF;
      break;
    case i::wasm::HeapType::kExtern:
      kind = ValKind::EXTERNREF;
      break;
    default:
      UNREACHABLE();
  }
  return TableType::make(ValType::make(kind), Limits(min, max));
}

// TODO(14034): Handle types other than funcref and externref if needed.
WASM_EXPORT auto Table::get(size_t index) const -> own<Ref> {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::DirectHandle<i::WasmTableObject> table = impl(this)->v8_object();
  if (index >= static_cast<size_t>(table->current_length())) return own<Ref>();
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  i::HandleScope handle_scope(isolate);
  i::DirectHandle<i::Object> result =
      i::WasmTableObject::Get(isolate, table, static_cast<uint32_t>(index));
  if (IsWasmFuncRef(*result)) {
    result = i::WasmInternalFunction::GetOrCreateExternal(i::direct_handle(
        i::Cast<i::WasmFuncRef>(*result)->internal(isolate), isolate));
  }
  if (IsWasmNull(*result)) {
    result = isolate->factory()->null_value();
  }
  DCHECK(IsNull(*result, isolate) || IsJSReceiver(*result));
  return V8RefValueToWasm(impl(this)->store(), result);
}

WASM_EXPORT auto Table::set(size_t index, const Ref* ref) -> bool {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::DirectHandle<i::WasmTableObject> table = impl(this)->v8_object();
  if (index >= static_cast<size_t>(table->current_length())) return false;
  i::HandleScope handle_scope(isolate);
  i::Handle<i::Object> obj = WasmRefToV8(isolate, ref);
  const char* error_message;
  // We can use `table->unsafe_type()` and `module == nullptr` here as long
  // as the C-API doesn't support indexed types.
  DCHECK(!table->unsafe_type().has_index());
  i::DirectHandle<i::Object> obj_as_wasm =
      i::wasm::JSToWasmObject(isolate, nullptr, obj, table->unsafe_type(),
                              &error_message)
          .ToHandleChecked();
  i::WasmTableObject::Set(isolate, table, static_cast<uint32_t>(index),
                          obj_as_wasm);
  return true;
}

// TODO(jkummerow): Having Table::size_t shadowing "std" size_t is ugly.
WASM_EXPORT auto Table::size() const -> size_t {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  return impl(this)->v8_object()->current_length();
}

WASM_EXPORT auto Table::grow(size_t delta, const Ref* ref) -> bool {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::DirectHandle<i::WasmTableObject> table = impl(this)->v8_object();
  i::HandleScope scope(isolate);
  i::Handle<i::Object> obj = WasmRefToV8(isolate, ref);
  const char* error_message;
  // We can use `table->unsafe_type()` and `module == nullptr` here as long
  // as the C-API doesn't support indexed types.
  DCHECK(!table->unsafe_type().has_index());
  i::DirectHandle<i::Object> obj_as_wasm =
      i::wasm::JSToWasmObject(isolate, nullptr, obj, table->unsafe_type(),
                              &error_message)
          .ToHandleChecked();
  int result = i::WasmTableObject::Grow(
      isolate, table, static_cast<uint32_t>(delta), obj_as_wasm);
  return result >= 0;
}

// Memory Instances

template <>
struct implement<Memory> {
  using type = RefImpl<Memory, i::WasmMemoryObject>;
};

WASM_EXPORT void Memory::destroy() { delete impl(this); }

WASM_EXPORT auto Memory::copy() const -> own<Memory> {
  return impl(this)->copy();
}

WASM_EXPORT auto Memory::make(Store* store_abs, const MemoryType* type)
    -> own<Memory> {
  StoreImpl* store = impl(store_abs);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope scope(isolate);
  CheckAndHandleInterrupts(isolate);

  const Limits& limits = type->limits();
  uint32_t minimum = limits.min;
  // The max_mem_pages limit is only spec'ed for JS embeddings, so we'll
  // directly use the maximum pages limit here.
  if (minimum > i::wasm::kSpecMaxMemory32Pages) return nullptr;
  uint32_t maximum = limits.max;
  if (maximum != Limits(0).max) {
    if (maximum < minimum) return nullptr;
    if (maximum > i::wasm::kSpecMaxMemory32Pages) return nullptr;
  }
  // TODO(wasm+): Support shared memory and memory64.
  i::SharedFlag shared = i::SharedFlag::kNotShared;
  i::wasm::AddressType address_type = i::wasm::AddressType::kI32;
  i::DirectHandle<i::WasmMemoryObject> memory_obj;
  if (!i::WasmMemoryObject::New(isolate, minimum, maximum, shared, address_type)
           .ToHandle(&memory_obj)) {
    return own<Memory>();
  }
  return implement<Memory>::type::make(store, memory_obj);
}

WASM_EXPORT auto Memory::type() const -> own<MemoryType> {
  PtrComprCageAccessScope ptr_compr_cage_access_scope(impl(this)->isolate());
  i::DirectHandle<i::WasmMemoryObject> memory = impl(this)->v8_object();
  uint32_t min = static_cast<uint32_t>(memory->array_buffer()->byte_length() /
                                       i::wasm::kWasmPageSize);
  uint32_t max =
      memory->has_maximum_pages() ? memory->maximum_pages() : 0xFFFFFFFFu;
  return MemoryType::make(Limits(min, max));
}

WASM_EXPORT auto Memory::data() const -> byte_t* {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  return reinterpret_cast<byte_t*>(
      impl(this)->v8_object()->array_buffer()->backing_store());
}

WASM_EXPORT auto Memory::data_size() const -> size_t {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  return impl(this)->v8_object()->array_buffer()->byte_length();
}

WASM_EXPORT auto Memory::size() const -> pages_t {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  return static_cast<pages_t>(
      impl(this)->v8_object()->array_buffer()->byte_length() /
      i::wasm::kWasmPageSize);
}

WASM_EXPORT auto Memory::grow(pages_t delta) -> bool {
  i::Isolate* isolate = impl(this)->isolate();
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate));
  i::HandleScope handle_scope(isolate);
  i::DirectHandle<i::WasmMemoryObject> memory = impl(this)->v8_object();
  int32_t old = i::WasmMemoryObject::Grow(isolate, memory, delta);
  return old != -1;
}

// Module Instances

template <>
struct implement<Instance> {
  using type = RefImpl<Instance, i::WasmInstanceObject>;
};

WASM_EXPORT void Instance::destroy() { delete impl(this); }
WASM_EXPORT auto Instance::copy() const -> own<Instance> {
  return impl(this)->copy();
}

WASM_EXPORT own<Instance> Instance::make(Store* store_abs,
                                         const Module* module_abs,
                                         const vec<Extern*>& imports,
                                         own<Trap>* trap) {
  StoreImpl* store = impl(store_abs);
  const implement<Module>::type* module = impl(module_abs);
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope handle_scope(isolate);
  CheckAndHandleInterrupts(isolate);

  DCHECK_EQ(module->v8_object()->GetIsolate(), isolate);

  if (trap) *trap = nullptr;
  ownvec<ImportType> import_types = module_abs->imports();
  i::Handle<i::JSObject> imports_obj =
      isolate->factory()->NewJSObject(isolate->object_function());
  for (size_t i = 0; i < import_types.size(); ++i) {
    ImportType* type = import_types[i].get();
    i::DirectHandle<i::String> module_str =
        VecToString(isolate, type->module());
    i::DirectHandle<i::String> name_str = VecToString(isolate, type->name());

    i::DirectHandle<i::JSObject> module_obj;
    i::LookupIterator module_it(isolate, imports_obj, module_str,
                                i::LookupIterator::OWN_SKIP_INTERCEPTOR);
    if (i::JSObject::HasProperty(&module_it).ToChecked()) {
      module_obj = i::Cast<i::JSObject>(
          i::Object::GetProperty(&module_it).ToHandleChecked());
    } else {
      module_obj = isolate->factory()->NewJSObject(isolate->object_function());
      ignore(
          i::Object::SetProperty(isolate, imports_obj, module_str, module_obj));
    }
    ignore(i::Object::SetProperty(isolate, module_obj, name_str,
                                  impl(imports[i])->v8_object()));
  }
  i::wasm::ErrorThrower thrower(isolate, "instantiation");
  i::MaybeDirectHandle<i::WasmInstanceObject> instance_obj =
      i::wasm::GetWasmEngine()->SyncInstantiate(
          isolate, &thrower, module->v8_object(), imports_obj,
          i::MaybeHandle<i::JSArrayBuffer>());
  if (trap) {
    if (thrower.error()) {
      *trap = implement<Trap>::type::make(
          store, GetProperException(isolate, thrower.Reify()));
      DCHECK(!thrower.error());           // Reify() called Reset().
      DCHECK(!isolate->has_exception());  // Hasn't been thrown yet.
      return own<Instance>();
    } else if (isolate->has_exception()) {
      i::Handle<i::Object> maybe_exception(isolate->exception(), isolate);
      *trap = implement<Trap>::type::make(
          store, GetProperException(isolate, maybe_exception));
      isolate->clear_exception();
      return own<Instance>();
    }
  } else if (instance_obj.is_null()) {
    // If no {trap} output is specified, silently swallow all errors.
    thrower.Reset();
    isolate->clear_exception();
    return own<Instance>();
  }
  return implement<Instance>::type::make(store, instance_obj.ToHandleChecked());
}

namespace {

own<Instance> GetInstance(StoreImpl* store,
                          i::DirectHandle<i::WasmInstanceObject> instance) {
  return implement<Instance>::type::make(store, instance);
}

}  // namespace

WASM_EXPORT auto Instance::exports() const -> ownvec<Extern> {
  const implement<Instance>::type* instance = impl(this);
  StoreImpl* store = instance->store();
  i::Isolate* isolate = store->i_isolate();
  v8::Isolate::Scope isolate_scope(store->isolate());
  i::HandleScope handle_scope(isolate);
  CheckAndHandleInterrupts(isolate);
  i::DirectHandle<i::WasmInstanceObject> instance_obj = instance->v8_object();
  i::DirectHandle<i::WasmModuleObject> module_obj(instance_obj->module_object(),
                                                  isolate);
  i::DirectHandle<i::JSObject> exports_obj(instance_obj->exports_object(),
                                           isolate);

  ownvec<ExportType> export_types = ExportsImpl(module_obj);
  ownvec<Extern> exports =
      ownvec<Extern>::make_uninitialized(export_types.size());
  if (!exports) return ownvec<Extern>::invalid();

  for (size_t i = 0; i < export_types.size(); ++i) {
    auto& name = export_types[i]->name();
    i::DirectHandle<i::String> name_str = VecToString(isolate, name);
    i::DirectHandle<i::Object> obj =
        i::Object::GetProperty(isolate, exports_obj, name_str)
            .ToHandleChecked();

    const ExternType* type = export_types[i]->type();
    switch (type->kind()) {
      case ExternKind::FUNC: {
        DCHECK(i::WasmExternalFunction::IsWasmExternalFunction(*obj));
        exports[i] = implement<Func>::type::make(
            store, i::Cast<i::WasmExternalFunction>(obj));
      } break;
      case ExternKind::GLOBAL: {
        exports[i] = implement<Global>::type::make(
            store, i::Cast<i::WasmGlobalObject>(obj));
      } break;
      case ExternKind::TABLE: {
        exports[i] = implement<Table>::type::make(
            store, i::Cast<i::WasmTableObject>(obj));
      } break;
      case ExternKind::MEMORY: {
        exports[i] = implement<Memory>::type::make(
            store, i::Cast<i::WasmMemoryObject>(obj));
      } break;
    }
  }

  return exports;
}

///////////////////////////////////////////////////////////////////////////////

}  // namespace wasm

// BEGIN FILE wasm-c.cc

extern "C" {

///////////////////////////////////////////////////////////////////////////////
// Auxiliaries

// Backing implementation

extern "C++" {

template <class T>
struct borrowed_vec {
  wasm::vec<T> it;
  explicit borrowed_vec(wasm::vec<T>&& v) : it(std::move(v)) {}
  borrowed_vec(borrowed_vec<T>&& that) : it(std::move(that.it)) {}
  ~borrowed_vec() { it.release(); }
};

}  // extern "C++"

#define WASM_DEFINE_OWN(name, Name)                                            \
  struct wasm_##name##_t : Name {};                                            \
                                                                               \
  void wasm_##name##_delete(wasm_##name##_t* x) { delete x; }                  \
                                                                               \
  extern "C++" inline auto hide_##name(Name* x)->wasm_##name##_t* {            \
    return static_cast<wasm_##name##_t*>(x);                                   \
  }                                                                            \
  extern "C++" inline auto hide_##name(const Name* x)                          \
      ->const wasm_##name##_t* {                                               \
    return static_cast<const wasm_##name##_t*>(x);                             \
  }                                                                            \
  extern "C++" inline auto reveal_##name(wasm_##name##_t* x)->Name* {          \
    return x;                                                                  \
  }                                                                            \
  extern "C++" inline auto reveal_##name(const wasm_##name##_t* x)             \
      ->const Name* {                                                          \
    return x;                                                                  \
  }                                                                            \
  extern "C++" inline auto get_##name(wasm::own<Name>& x)->wasm_##name##_t* {  \
    return hide_##name(x.get());                                               \
  }                                                                            \
  extern "C++" inline auto get_##name(const wasm::own<Name>& x)                \
      ->const wasm_##name##_t* {                                               \
    return hide_##name(x.get());                                               \
  }                                                                            \
  extern "C++" inline auto release_##name(wasm::own<Name>&& x)                 \
      ->wasm_##name##_t* {                                                     \
    return hide_##name(x.release());                                           \
  }                                                                            \
  extern "C++" inline auto adopt_##name(wasm_##name##_t* x)->wasm::own<Name> { \
    return make_own(x);                                                        \
  }

// Vectors

#ifdef V8_GC_MOLE
#define ASSERT_VEC_BASE_SIZE(name, Name, vec, ptr_or_none)

#else
#define ASSERT_VEC_BASE_SIZE(name, Name, vec, ptr_or_none)                 \
  static_assert(sizeof(wasm_##name##_vec_t) == sizeof(vec<Name>),          \
                "C/C++ incompatibility");                                  \
  static_assert(                                                           \
      sizeof(wasm_##name##_t ptr_or_none) == sizeof(vec<Name>::elem_type), \
      "C/C++ incompatibility");
#endif

#define WASM_DEFINE_VEC_BASE(name, Name, vec, ptr_or_none)                     \
  ASSERT_VEC_BASE_SIZE(name, Name, vec, ptr_or_none)                           \
  extern "C++" inline auto hide_##name##_vec(vec<Name>& v)                     \
      ->wasm_##name##_vec_t* {                                                 \
    return reinterpret_cast<wasm_##name##_vec_t*>(&v);                         \
  }                                                                            \
  extern "C++" inline auto hide_##name##_vec(const vec<Name>& v)               \
      ->const wasm_##name##_vec_t* {                                           \
    return reinterpret_cast<const wasm_##name##_vec_t*>(&v);                   \
  }                                                                            \
  extern "C++" inline auto hide_##name##_vec(vec<Name>::elem_type* v)          \
      ->wasm_##name##_t ptr_or_none* {                                         \
    return reinterpret_cast<wasm_##name##_t ptr_or_none*>(v);                  \
  }                                                                            \
  extern "C++" inline auto hide_##name##_vec(const vec<Name>::elem_type* v)    \
      ->wasm_##name##_t ptr_or_none const* {                                   \
    return reinterpret_cast<wasm_##name##_t ptr_or_none const*>(v);            \
  }                                                                            \
  extern "C++" inline auto reveal_##name##_vec(wasm_##name##_t ptr_or_none* v) \
      ->vec<Name>::elem_type* {                                                \
    return reinterpret_cast<vec<Name>::elem_type*>(v);                         \
  }                                                                            \
  extern "C++" inline auto reveal_##name##_vec(                                \
      wasm_##name##_t ptr_or_none const* v)                                    \
      ->const vec<Name>::elem_type* {                                          \
    return reinterpret_cast<const vec<Name>::elem_type*>(v);                   \
  }                                                                            \
  extern "C++" inline auto get_##name##_vec(vec<Name>& v)                      \
      ->wasm_##name##_vec_t {                                                  \
    wasm_##name##_vec_t v2 = {v.size(), hide_##name##_vec(v.get())};           \
    return v2;                                                                 \
  }                                                                            \
  extern "C++" inline auto get_##name##_vec(const vec<Name>& v)                \
      ->const wasm_##name##_vec_t {                                            \
    wasm_##name##_vec_t v2 = {                                                 \
        v.size(),                                                              \
        const_cast<wasm_##name##_t ptr_or_none*>(hide_##name##_vec(v.get()))}; \
    return v2;                                                                 \
  }                                                                            \
  extern "C++" inline auto release_##name##_vec(vec<Name>&& v)                 \
      ->wasm_##name##_vec_t {                                                  \
    wasm_##name##_vec_t v2 = {v.size(), hide_##name##_vec(v.release())};       \
    return v2;                                                                 \
  }                                                                            \
  extern "C++" inline auto adopt_##name##_vec(wasm_##name##_vec_t* v)          \
      ->vec<Name> {                                                            \
    return vec<Name>::adopt(v->size, reveal_##name##_vec(v->data));            \
  }                                                                            \
  extern "C++" inline auto adopt_##name##_vec(const wasm_##name##_vec_t* v)    \
      ->const vec<Name> {                                                      \
    return vec<Name>::adopt(v->size, reveal_##name##_vec(v->data));            \
  }                                                                            \
  extern "C++" inline auto borrow_##name##_vec(const wasm_##name##_vec_t* v)   \
      ->borrowed_vec<vec<Name>::elem_type> {                                   \
    return borrowed_vec<vec<Name>::elem_type>(                                 \
        vec<Name>::adopt(v->size, reveal_##name##_vec(v->data)));              \
  }                                                                            \
                                                                               \
  void wasm_##name##_vec_new_uninitialized(wasm_##name##_vec_t* out,           \
                                           size_t size) {                      \
    *out = release_##name##_vec(vec<Name>::make_uninitialized(size));          \
  }                                                                            \
  void wasm_##name##_vec_new_empty(wasm_##name##_vec_t* out) {                 \
    wasm_##name##_vec_new_uninitialized(out, 0);                               \
  }                                                                            \
                                                                               \
  void wasm_##name##_vec_delete(wasm_##name##_vec_t* v) {                      \
    adopt_##name##_vec(v);                                                     \
  }

// Vectors with no ownership management of elements
#define WASM_DEFINE_VEC_PLAIN(name, Name)                                     \
  WASM_DEFINE_VEC_BASE(name, Name,                                            \
                       wasm::vec, ) /* NOLINT(whitespace/parens) */           \
                                                                              \
  void wasm_##name##_vec_new(wasm_##name##_vec_t* out, size_t size,           \
                             const wasm_##name##_t data[]) {                  \
    auto v2 = wasm::vec<Name>::make_uninitialized(size);                      \
    if (v2.size() != 0) {                                                     \
      memcpy(v2.get(), data, size * sizeof(wasm_##name##_t));                 \
    }                                                                         \
    *out = release_##name##_vec(std::move(v2));                               \
  }                                                                           \
                                                                              \
  WASM_API_EXTERN void wasm_##name##_vec_copy(wasm_##name##_vec_t* out,       \
                                              const wasm_##name##_vec_t* v) { \
    wasm_##name##_vec_new(out, v->size, v->data);                             \
  }

// Vectors that own their elements
#define WASM_DEFINE_VEC_OWN(name, Name)                                       \
  WASM_DEFINE_VEC_BASE(name, Name, wasm::ownvec, *)                           \
                                                                              \
  void wasm_##name##_vec_new(wasm_##name##_vec_t* out, size_t size,           \
                             wasm_##name##_t* const data[]) {                 \
    auto v2 = wasm::ownvec<Name>::make_uninitialized(size);                   \
    for (size_t i = 0; i < v2.size(); ++i) {                                  \
      v2[i] = adopt_##name(data[i]);                                          \
    }                                                                         \
    *out = release_##name##_vec(std::move(v2));                               \
  }                                                                           \
                                                                              \
  WASM_API_EXTERN void wasm_##name##_vec_copy(wasm_##name##_vec_t* out,       \
                                              const wasm_##name##_vec_t* v) { \
    auto v2 = wasm::ownvec<Name>::make_uninitialized(v->size);                \
    for (size_t i = 0; i < v2.size(); ++i) {                                  \
      v2[i] = adopt_##name(wasm_##name##_copy(v->data[i]));                   \
    }                                                                         \
    *out = release_##name##_vec(std::move(v2));                               \
  }

extern "C++" {
template <class T>
inline auto is_empty(T* p) -> bool {
  return !p;
}
}

// Byte vectors

WASM_DEFINE_VEC_PLAIN(byte, byte_t)

///////////////////////////////////////////////////////////////////////////////
// Runtime Environment

// Configuration

WASM_DEFINE_OWN(config, wasm::Config)

wasm_config_t* wasm_config_new() {
  return release_config(wasm::Config::make());
}

// Engine

WASM_DEFINE_OWN(engine, wasm::Engine)

wasm_engine_t* wasm_engine_new() {
  return release_engine(wasm::Engine::make());
}

wasm_engine_t* wasm_engine_new_with_config(wasm_config_t* config) {
  return release_engine(wasm::Engine::make(adopt_config(config)));
}

// Stores

WASM_DEFINE_OWN(store, wasm::Store)

wasm_store_t* wasm_store_new(wasm_engine_t* engine) {
  return release_store(wasm::Store::make(engine));
}

///////////////////////////////////////////////////////////////////////////////
// Type Representations

// Type attributes

extern "C++" inline auto hide_mutability(wasm::Mutability mutability)
    -> wasm_mutability_t {
  return static_cast<wasm_mutability_t>(mutability);
}

extern "C++" inline auto reveal_mutability(wasm_mutability_t mutability)
    -> wasm::Mutability {
  return static_cast<wasm::Mutability>(mutability);
}

extern "C++" inline auto hide_limits(const wasm::Limits& limits)
    -> const wasm_limits_t* {
  return reinterpret_cast<const wasm_limits_t*>(&limits);
}

extern "C++" inline auto reveal_limits(wasm_limits_t limits) -> wasm::Limits {
  return wasm::Limits(limits.min, limits.max);
}

extern "C++" inline auto hide_valkind(wasm::ValKind kind) -> wasm_valkind_t {
  return static_cast<wasm_valkind_t>(kind);
}

extern "C++" inline auto reveal_valkind(wasm_valkind_t kind) -> wasm::ValKind {
  return static_cast<wasm::ValKind>(kind);
}

extern "C++" inline auto hide_externkind(wasm::ExternKind kind)
    -> wasm_externkind_t {
  return static_cast<wasm_externkind_t>(kind);
}

extern "C++" inline auto reveal_externkind(wasm_externkind_t kind)
    -> wasm::ExternKind {
  return static_cast<wasm::ExternKind>(kind);
}

// Generic

#define WASM_DEFINE_TYPE(name, Name)                   \
  WASM_DEFINE_OWN(name, Name)                          \
  WASM_DEFINE_VEC_OWN(name, Name)                      \
                                                       \
  WASM_API_EXTERN wasm_##name##_t* wasm_##name##_copy( \
      const wasm_##name##_t* t) {                      \
    return release_##name(t->copy());                  \
  }

// Value Types

WASM_DEFINE_TYPE(valtype, wasm::ValType)

wasm_valtype_t* wasm_valtype_new(wasm_valkind_t k) {
  return release_valtype(wasm::ValType::make(reveal_valkind(k)));
}

wasm_valkind_t wasm_valtype_kind(const wasm_valtype_t* t) {
  return hide_valkind(t->kind());
}

// Function Types

WASM_DEFINE_TYPE(functype, wasm::FuncType)

wasm_functype_t* wasm_functype_new(wasm_valtype_vec_t* params,
                                   wasm_valtype_vec_t* results) {
  return release_functype(wasm::FuncType::make(adopt_valtype_vec(params),
                                               adopt_valtype_vec(results)));
}

const wasm_valtype_vec_t* wasm_functype_params(const wasm_functype_t* ft) {
  return hide_valtype_vec(ft->params());
}

const wasm_valtype_vec_t* wasm_functype_results(const wasm_functype_t* ft) {
  return hide_valtype_vec(ft->results());
}

// Global Types

WASM_DEFINE_TYPE(globaltype, wasm::GlobalType)

wasm_globaltype_t* wasm_globaltype_new(wasm_valtype_t* content,
                                       wasm_mutability_t mutability) {
  return release_globaltype(wasm::GlobalType::make(
      adopt_valtype(content), reveal_mutability(mutability)));
}

const wasm_valtype_t* wasm_globaltype_content(const wasm_globaltype_t* gt) {
  return hide_valtype(gt->content());
}

wasm_mutability_t wasm_globaltype_mutability(const wasm_globaltype_t* gt) {
  return hide_mutability(gt->mutability());
}

// Table Types

WASM_DEFINE_TYPE(tabletype, wasm::TableType)

wasm_tabletype_t* wasm_tabletype_new(wasm_valtype_t* element,
                                     const wasm_limits_t* limits) {
  return release_tabletype(
      wasm::TableType::make(adopt_valtype(element), reveal_limits(*limits)));
}

const wasm_valtype_t* wasm_tabletype_element(const wasm_tabletype_t* tt) {
  return hide_valtype(tt->element());
}

const wasm_limits_t* wasm_tabletype_limits(const wasm_tabletype_t* tt) {
  return hide_limits(tt->limits());
}

// Memory Types

WASM_DEFINE_TYPE(memorytype, wasm::MemoryType)

wasm_memorytype_t* wasm_memorytype_new(const wasm_limits_t* limits) {
  return release_memorytype(wasm::MemoryType::make(reveal_limits(*limits)));
}

const wasm_limits_t* wasm_memorytype_limits(const wasm_memorytype_t* mt) {
  return hide_limits(mt->limits());
}

// Extern Types

WASM_DEFINE_TYPE(externtype, wasm::ExternType)

wasm_externkind_t wasm_externtype_kind(const wasm_externtype_t* et) {
  return hide_externkind(et->kind());
}

wasm_externtype_t* wasm_functype_as_externtype(wasm_functype_t* ft) {
  return hide_externtype(static_cast<wasm::ExternType*>(ft));
}
wasm_externtype_t* wasm_globaltype_as_externtype(wasm_globaltype_t* gt) {
  return hide_externtype(static_cast<wasm::ExternType*>(gt));
}
wasm_externtype_t* wasm_tabletype_as_externtype(wasm_tabletype_t* tt) {
  return hide_externtype(static_cast<wasm::ExternType*>(tt));
}
wasm_externtype_t* wasm_memorytype_as_externtype(wasm_memorytype_t* mt) {
  return hide_externtype(static_cast<wasm::ExternType*>(mt));
}

const wasm_externtype_t* wasm_functype_as_externtype_const(
    const wasm_functype_t* ft) {
  return hide_externtype(static_cast<const wasm::ExternType*>(ft));
}
const wasm_externtype_t* wasm_globaltype_as_externtype_const(
    const wasm_globaltype_t* gt) {
  return hide_externtype(static_cast<const wasm::ExternType*>(gt));
}
const wasm_externtype_t* wasm_tabletype_as_externtype_const(
    const wasm_tabletype_t* tt) {
  return hide_externtype(static_cast<const wasm::ExternType*>(tt));
}
const wasm_externtype_t* wasm_memorytype_as_externtype_const(
    const wasm_memorytype_t* mt) {
  return hide_externtype(static_cast<const wasm::ExternType*>(mt));
}

wasm_functype_t* wasm_externtype_as_functype(wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::FUNC
             ? hide_functype(
                   static_cast<wasm::FuncType*>(reveal_externtype(et)))
             : nullptr;
}
wasm_globaltype_t* wasm_externtype_as_globaltype(wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::GLOBAL
             ? hide_globaltype(
                   static_cast<wasm::GlobalType*>(reveal_externtype(et)))
             : nullptr;
}
wasm_tabletype_t* wasm_externtype_as_tabletype(wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::TABLE
             ? hide_tabletype(
                   static_cast<wasm::TableType*>(reveal_externtype(et)))
             : nullptr;
}
wasm_memorytype_t* wasm_externtype_as_memorytype(wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::MEMORY
             ? hide_memorytype(
                   static_cast<wasm::MemoryType*>(reveal_externtype(et)))
             : nullptr;
}

const wasm_functype_t* wasm_externtype_as_functype_const(
    const wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::FUNC
             ? hide_functype(
                   static_cast<const wasm::FuncType*>(reveal_externtype(et)))
             : nullptr;
}
const wasm_globaltype_t* wasm_externtype_as_globaltype_const(
    const wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::GLOBAL
             ? hide_globaltype(
                   static_cast<const wasm::GlobalType*>(reveal_externtype(et)))
             : nullptr;
}
const wasm_tabletype_t* wasm_externtype_as_tabletype_const(
    const wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::TABLE
             ? hide_tabletype(
                   static_cast<const wasm::TableType*>(reveal_externtype(et)))
             : nullptr;
}
const wasm_memorytype_t* wasm_externtype_as_memorytype_const(
    const wasm_externtype_t* et) {
  return et->kind() == wasm::ExternKind::MEMORY
             ? hide_memorytype(
                   static_cast<const wasm::MemoryType*>(reveal_externtype(et)))
             : nullptr;
}

// Import Types

WASM_DEFINE_TYPE(importtype, wasm::ImportType)

wasm_importtype_t* wasm_importtype_new(wasm_name_t* module, wasm_name_t* name,
                                       wasm_externtype_t* type) {
  return release_importtype(wasm::ImportType::make(
      adopt_byte_vec(module), adopt_byte_vec(name), adopt_externtype(type)));
}

const wasm_name_t* wasm_importtype_module(const wasm_importtype_t* it) {
  return hide_byte_vec(it->module());
}

const wasm_name_t* wasm_importtype_name(const wasm_importtype_t* it) {
  return hide_byte_vec(it->name());
}

const wasm_externtype_t* wasm_importtype_type(const wasm_importtype_t* it) {
  return hide_externtype(it->type());
}

// Export Types

WASM_DEFINE_TYPE(exporttype, wasm::ExportType)

wasm_exporttype_t* wasm_exporttype_new(wasm_name_t* name,
                                       wasm_externtype_t* type) {
  return release_exporttype(
      wasm::ExportType::make(adopt_byte_vec(name), adopt_externtype(type)));
}

const wasm_name_t* wasm_exporttype_name(const wasm_exporttype_t* et) {
  return hide_byte_vec(et->name());
}

const wasm_externtype_t* wasm_exporttype_type(const wasm_exporttype_t* et) {
  return hide_externtype(et->type());
}

///////////////////////////////////////////////////////////////////////////////
// Runtime Values

// References

#define WASM_DEFINE_REF_BASE(name, Name)                             \
  WASM_DEFINE_OWN(name, Name)                                        \
                                                                     \
  wasm_##name##_t* wasm_##name##_copy(const wasm_##name##_t* t) {    \
    return release_##name(t->copy());                                \
  }                                                                  \
                                                                     \
  bool wasm_##name##_same(const wasm_##name##_t* t1,                 \
                          const wasm_##name##_t* t2) {               \
    return t1->same(t2);                                             \
  }                                                                  \
                                                                     \
  void* wasm_##name##_get_host_info(const wasm_##name##_t* r) {      \
    return r->get_host_info();                                       \
  }                                                                  \
  void wasm_##name##_set_host_info(wasm_##name##_t* r, void* info) { \
    r->set_host_info(info);                                          \
  }                                                                  \
  void wasm_##name##_set_host_info_with_finalizer(                   \
      wasm_##name##_t* r, void* info, void (*finalizer)(void*)) {    \
    r->set_host_info(info, finalizer);                               \
  }

#define WASM_DEFINE_REF(name, Name)                                        \
  WASM_DEFINE_REF_BASE(name, Name)                                         \
                                                                           \
  wasm_ref_t* wasm_##name##_as_ref(wasm_##name##_t* r) {                   \
    return hide_ref(static_cast<wasm::Ref*>(reveal_##name(r)));            \
  }                                                                        \
  wasm_##name##_t* wasm_ref_as_##name(wasm_ref_t* r) {                     \
    return hide_##name(static_cast<Name*>(reveal_ref(r)));                 \
  }                                                                        \
                                                                           \
  const wasm_ref_t* wasm_##name##_as_ref_const(const wasm_##name##_t* r) { \
    return hide_ref(static_cast<const wasm::Ref*>(reveal_##name(r)));      \
  }                                                                        \
  const wasm_##name##_t* wasm_ref_as_##name##_const(const wasm_ref_t* r) { \
    return hide_##name(static_cast<const Name*>(reveal_ref(r)));           \
  }

#define WASM_DEFINE_SHARABLE_REF(name, Name) \
  WASM_DEFINE_REF(name, Name)                \
  WASM_DEFINE_OWN(shared_##name, wasm::Shared<Name>)

WASM_DEFINE_REF_BASE(ref, wasm::Ref)

// Values

extern "C++" {

inline auto is_empty(wasm_val_t v) -> bool {
  return !is_ref(reveal_valkind(v.kind)) || !v.of.ref;
}

inline auto hide_val(wasm::Val v) -> wasm_val_t {
  wasm_val_t v2 = {hide_valkind(v.kind()), {}};
  switch (v.kind()) {
    case wasm::ValKind::I32:
      v2.of.i32 = v.i32();
      break;
    case wasm::ValKind::I64:
      v2.of.i64 = v.i64();
      break;
    case wasm::ValKind::F32:
      v2.of.f32 = v.f32();
      break;
    case wasm::ValKind::F64:
      v2.of.f64 = v.f64();
      break;
    case wasm::ValKind::EXTERNREF:
    case wasm::ValKind::FUNCREF:
      v2.of.ref = hide_ref(v.ref());
      break;
    default:
      UNREACHABLE();
  }
  return v2;
}

inline auto release_val(wasm::Val v) -> wasm_val_t {
  wasm_val_t v2 = {hide_valkind(v.kind()), {}};
  switch (v.kind()) {
    case wasm::ValKind::I32:
      v2.of.i32 = v.i32();
      break;
    case wasm::ValKind::I64:
      v2.of.i64 = v.i64();
      break;
    case wasm::ValKind::F32:
      v2.of.f32 = v.f32();
      break;
    case wasm::ValKind::F64:
      v2.of.f64 = v.f64();
      break;
    case wasm::ValKind::EXTERNREF:
    case wasm::ValKind::FUNCREF:
      v2.of.ref = release_ref(v.release_ref());
      break;
    default:
      UNREACHABLE();
  }
  return v2;
}

inline auto adopt_val(wasm_val_t v) -> wasm::Val {
  switch (reveal_valkind(v.kind)) {
    case wasm::ValKind::I32:
      return wasm::Val(v.of.i32);
    case wasm::ValKind::I64:
      return wasm::Val(v.of.i64);
    case wasm::ValKind::F32:
      return wasm::Val(v.of.f32);
    case wasm::ValKind::F64:
      return wasm::Val(v.of.f64);
    case wasm::ValKind::EXTERNREF:
    case wasm::ValKind::FUNCREF:
      return wasm::Val(adopt_ref(v.of.ref));
    default:
      UNREACHABLE();
  }
}

struct borrowed_val {
  wasm::Val it;
  explicit borrowed_val(wasm::Val&& v) : it(std::move(v)) {}
  borrowed_val(borrowed_val&& that) : it(std::move(that.it)) {}
  ~borrowed_val() {
    if (it.is_ref()) it.release_ref().release();
  }
};

inline auto borrow_val(const wasm_val_t* v) -> borrowed_val {
  wasm::Val v2;
  switch (reveal_valkind(v->kind)) {
    case wasm::ValKind::I32:
      v2 = wasm::Val(v->of.i32);
      break;
    case wasm::ValKind::I64:
      v2 = wasm::Val(v->of.i64);
      break;
    case wasm::ValKind::F32:
      v2 = wasm::Val(v->of.f32);
      break;
    case wasm::ValKind::F64:
      v2 = wasm::Val(v->of.f64);
      break;
    case wasm::ValKind::EXTERNREF:
    case wasm::ValKind::FUNCREF:
      v2 = wasm::Val(adopt_ref(v->of.ref));
      break;
    default:
      UNREACHABLE();
  }
  return borrowed_val(std::move(v2));
}

}  // extern "C++"

WASM_DEFINE_VEC_BASE(val, wasm::Val, wasm::vec, )

void wasm_val_vec_new(wasm_val_vec_t* out, size_t size,
                      wasm_val_t const data[]) {
  auto v2 = wasm::vec<wasm::Val>::make_uninitialized(size);
  for (size_t i = 0; i < v2.size(); ++i) {
    v2[i] = adopt_val(data[i]);
  }
  *out = release_val_vec(std::move(v2));
}

void wasm_val_vec_copy(wasm_val_vec_t* out, const wasm_val_vec_t* v) {
  auto v2 = wasm::vec<wasm::Val>::make_uninitialized(v->size);
  for (size_t i = 0; i < v2.size(); ++i) {
    wasm_val_t val;
    wasm_val_copy(&v->data[i], &val);
    v2[i] = adopt_val(val);
  }
  *out = release_val_vec(std::move(v2));
}

void wasm_val_delete(wasm_val_t* v) {
  if (is_ref(reveal_valkind(v->kind))) {
    adopt_ref(v->of.ref);
  }
}

void wasm_val_copy(wasm_val_t* out, const wasm_val_t* v) {
  *out = *v;
  if (is_ref(reveal_valkind(v->kind))) {
    out->of.ref = v->of.ref ? release_ref(v->of.ref->copy()) : nullptr;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Runtime Objects

// Frames

WASM_DEFINE_OWN(frame, wasm::Frame)
WASM_DEFINE_VEC_OWN(frame, wasm::Frame)

wasm_frame_t* wasm_frame_copy(const wasm_frame_t* frame) {
  return release_frame(frame->copy());
}

wasm_instance_t* wasm_frame_instance(const wasm_frame_t* frame);
// Defined below along with wasm_instance_t.

uint32_t wasm_frame_func_index(const wasm_frame_t* frame) {
  return reveal_frame(frame)->func_index();
}

size_t wasm_frame_func_offset(const wasm_frame_t* frame) {
  return reveal_frame(frame)->func_offset();
}

size_t wasm_frame_module_offset(const wasm_frame_t* frame) {
  return reveal_frame(frame)->module_offset();
}

// Traps

WASM_DEFINE_REF(trap, wasm::Trap)

wasm_trap_t* wasm_trap_new(wasm_store_t* store, const wasm_message_t* message) {
  auto message_ = borrow_byte_vec(message);
  return release_trap(wasm::Trap::make(store, message_.it));
}

void wasm_trap_message(const wasm_trap_t* trap, wasm_message_t* out) {
  *out = release_byte_vec(reveal_trap(trap)->message());
}

wasm_frame_t* wasm_trap_origin(const wasm_trap_t* trap) {
  return release_frame(reveal_trap(trap)->origin());
}

void wasm_trap_trace(const wasm_trap_t* trap, wasm_frame_vec_t* out) {
  *out = release_frame_vec(reveal_trap(trap)->trace());
}

// Foreign Objects

WASM_DEFINE_REF(foreign, wasm::Foreign)

wasm_foreign_t* wasm_foreign_new(wasm_store_t* store) {
  return release_foreign(wasm::Foreign::make(store));
}

// Modules

WASM_DEFINE_SHARABLE_REF(module, wasm::Module)

bool wasm_module_validate(wasm_store_t* store, const wasm_byte_vec_t* binary) {
  auto binary_ = borrow_byte_vec(binary);
  return wasm::Module::validate(store, binary_.it);
}

wasm_module_t* wasm_module_new(wasm_store_t* store,
                               const wasm_byte_vec_t* binary) {
  auto binary_ = borrow_byte_vec(binary);
  return release_module(wasm::Module::make(store, binary_.it));
}

void wasm_module_imports(const wasm_module_t* module,
                         wasm_importtype_vec_t* out) {
  *out = release_importtype_vec(reveal_module(module)->imports());
}

void wasm_module_exports(const wasm_module_t* module,
                         wasm_exporttype_vec_t* out) {
  *out = release_exporttype_vec(reveal_module(module)->exports());
}

void wasm_module_serialize(const wasm_module_t* module, wasm_byte_vec_t* out) {
  *out = release_byte_vec(reveal_module(module)->serialize());
}

wasm_module_t* wasm_module_deserialize(wasm_store_t* store,
                                       const wasm_byte_vec_t* binary) {
  auto binary_ = borrow_byte_vec(binary);
  return release_module(wasm::Module::deserialize(store, binary_.it));
}

wasm_shared_module_t* wasm_module_share(const wasm_module_t* module) {
  return release_shared_module(reveal_module(module)->share());
}

wasm_module_t* wasm_module_obtain(wasm_store_t* store,
                                  const wasm_shared_module_t* shared) {
  return release_module(wasm::Module::obtain(store, shared));
}

// Function Instances

WASM_DEFINE_REF(func, wasm::Func)

extern "C++" {

auto wasm_callback(void* env, const wasm::vec<wasm::Val>& args,
                   wasm::vec<wasm::Val>& results) -> wasm::own<wasm::Trap> {
  auto f = reinterpret_cast<wasm_func_callback_t>(env);
  return adopt_trap(f(hide_val_vec(args), hide_val_vec(results)));
}

struct wasm_callback_env_t {
  wasm_func_callback_with_env_t callback;
  void* env;
  void (*finalizer)(void*);
};

auto wasm_callback_with_env(void* env, const wasm::vec<wasm::Val>& args,
                            wasm::vec<wasm::Val>& results)
    -> wasm::own<wasm::Trap> {
  auto t = static_cast<wasm_callback_env_t*>(env);
  return adopt_trap(
      t->callback(t->env, hide_val_vec(args), hide_val_vec(results)));
}

void wasm_callback_env_finalizer(void* env) {
  auto t = static_cast<wasm_callback_env_t*>(env);
  if (t->finalizer) t->finalizer(t->env);
  delete t;
}

}  // extern "C++"

wasm_func_t* wasm_func_new(wasm_store_t* store, const wasm_functype_t* type,
                           wasm_func_callback_t callback) {
  return release_func(wasm::Func::make(store, type, wasm_callback,
                                       reinterpret_cast<void*>(callback)));
}

wasm_func_t* wasm_func_new_with_env(wasm_store_t* store,
                                    const wasm_functype_t* type,
                                    wasm_func_callback_with_env_t callback,
                                    void* env, void (*finalizer)(void*)) {
  auto env2 = new wasm_callback_env_t{callback, env, finalizer};
  return release_func(wasm::Func::make(store, type, wasm_callback_with_env,
                                       env2, wasm_callback_env_finalizer));
}

wasm_functype_t* wasm_func_type(const wasm_func_t* func) {
  return release_functype(func->type());
}

size_t wasm_func_param_arity(const wasm_func_t* func) {
  return func->param_arity();
}

size_t wasm_func_result_arity(const wasm_func_t* func) {
  return func->result_arity();
}

WASM_API_EXTERN wasm_trap_t* wasm_func_call(const wasm_func_t* func,
                                            const wasm_val_vec_t* args,
                                            wasm_val_vec_t* results) {
  auto v8_results = adopt_val_vec(results);
  auto ret = release_trap(func->call(adopt_val_vec(args), v8_results));
  *results = release_val_vec(std::move(v8_results));

  return ret;
}

// Global Instances

WASM_DEFINE_REF(global, wasm::Global)

wasm_global_t* wasm_global_new(wasm_store_t* store,
                               const wasm_globaltype_t* type,
                               const wasm_val_t* val) {
  auto val_ = borrow_val(val);
  return release_global(wasm::Global::make(store, type, val_.it));
}

wasm_globaltype_t* wasm_global_type(const wasm_global_t* global) {
  return release_globaltype(global->type());
}

void wasm_global_get(const wasm_global_t* global, wasm_val_t* out) {
  *out = release_val(global->get());
}

void wasm_global_set(wasm_global_t* global, const wasm_val_t* val) {
  auto val_ = borrow_val(val);
  global->set(val_.it);
}

// Table Instances

WASM_DEFINE_REF(table, wasm::Table)

wasm_table_t* wasm_table_new(wasm_store_t* store, const wasm_tabletype_t* type,
                             wasm_ref_t* ref) {
  return release_table(wasm::Table::make(store, type, ref));
}

wasm_tabletype_t* wasm_table_type(const wasm_table_t* table) {
  return release_tabletype(table->type());
}

wasm_ref_t* wasm_table_get(const wasm_table_t* table, wasm_table_size_t index) {
  return release_ref(table->get(index));
}

bool wasm_table_set(wasm_table_t* table, wasm_table_size_t index,
                    wasm_ref_t* ref) {
  return table->set(index, ref);
}

wasm_table_size_t wasm_table_size(const wasm_table_t* table) {
  return table->size();
}

bool wasm_table_grow(wasm_table_t* table, wasm_table_size_t delta,
                     wasm_ref_t* ref) {
  return table->grow(delta, ref);
}

// Memory Instances

WASM_DEFINE_REF(memory, wasm::Memory)

wasm_memory_t* wasm_memory_new(wasm_store_t* store,
                               const wasm_memorytype_t* type) {
  return release_memory(wasm::Memory::make(store, type));
}

wasm_memorytype_t* wasm_memory_type(const wasm_memory_t* memory) {
  return release_memorytype(memory->type());
}

wasm_byte_t* wasm_memory_data(wasm_memory_t* memory) { return memory->data(); }

size_t wasm_memory_data_size(const wasm_memory_t* memory) {
  return memory->data_size();
}

wasm_memory_pages_t wasm_memory_size(const wasm_memory_t* memory) {
  return memory->size();
}

bool wasm_memory_grow(wasm_memory_t* memory, wasm_memory_pages_t delta) {
  return memory->grow(delta);
}

// Externals

WASM_DEFINE_REF(extern, wasm::Extern)
WASM_DEFINE_VEC_OWN(extern, wasm::Extern)

wasm_externkind_t wasm_extern_kind(const wasm_extern_t* external) {
  return hide_externkind(external->kind());
}
wasm_externtype_t* wasm_extern_type(const wasm_extern_t* external) {
  return release_externtype(external->type());
}

wasm_extern_t* wasm_func_as_extern(wasm_func_t* func) {
  return hide_extern(static_cast<wasm::Extern*>(reveal_func(func)));
}
wasm_extern_t* wasm_global_as_extern(wasm_global_t* global) {
  return hide_extern(static_cast<wasm::Extern*>(reveal_global(global)));
}
wasm_extern_t* wasm_table_as_extern(wasm_table_t* table) {
  return hide_extern(static_cast<wasm::Extern*>(reveal_table(table)));
}
wasm_extern_t* wasm_memory_as_extern(wasm_memory_t* memory) {
  return hide_extern(static_cast<wasm::Extern*>(reveal_memory(memory)));
}

const wasm_extern_t* wasm_func_as_extern_const(const wasm_func_t* func) {
  return hide_extern(static_cast<const wasm::Extern*>(reveal_func(func)));
}
const wasm_extern_t* wasm_global_as_extern_const(const wasm_global_t* global) {
  return hide_extern(static_cast<const wasm::Extern*>(reveal_global(global)));
}
const wasm_extern_t* wasm_table_as_extern_const(const wasm_table_t* table) {
  return hide_extern(static_cast<const wasm::Extern*>(reveal_table(table)));
}
const wasm_extern_t* wasm_memory_as_extern_const(const wasm_memory_t* memory) {
  return hide_extern(static_cast<const wasm::Extern*>(reveal_memory(memory)));
}

wasm_func_t* wasm_extern_as_func(wasm_extern_t* external) {
  return hide_func(external->func());
}
wasm_global_t* wasm_extern_as_global(wasm_extern_t* external) {
  return hide_global(external->global());
}
wasm_table_t* wasm_extern_as_table(wasm_extern_t* external) {
  return hide_table(external->table());
}
wasm_memory_t* wasm_extern_as_memory(wasm_extern_t* external) {
  return hide_memory(external->memory());
}

const wasm_func_t* wasm_extern_as_func_const(const wasm_extern_t* external) {
  return hide_func(external->func());
}
const wasm_global_t* wasm_extern_as_global_const(
    const wasm_extern_t* external) {
  return hide_global(external->global());
}
const wasm_table_t* wasm_extern_as_table_const(const wasm_extern_t* external) {
  return hide_table(external->table());
}
const wasm_memory_t* wasm_extern_as_memory_const(
    const wasm_extern_t* external) {
  return hide_memory(external->memory());
}

// Module Instances

WASM_DEFINE_REF(instance, wasm::Instance)

WASM_API_EXTERN wasm_instance_t* wasm_instance_new(
    wasm_store_t* store, const wasm_module_t* module,
    const wasm_extern_vec_t* imports, wasm_trap_t** trap) {
  wasm::own<wasm::Trap> error;

  size_t size = 0;
  if (imports->data && imports->size > 0) {
    size = imports->size;
  }

  auto v8_imports = wasm::vec<wasm::Extern*>::make_uninitialized(size);

  for (size_t i = 0; i < v8_imports.size(); i++) {
    v8_imports[i] = reveal_extern(imports->data[i]);
  }

  wasm_instance_t* instance =
      release_instance(wasm::Instance::make(store, module, v8_imports, &error));
  if (trap) *trap = hide_trap(error.release());
  return instance;
}

void wasm_instance_exports(const wasm_instance_t* instance,
                           wasm_extern_vec_t* out) {
  *out = release_extern_vec(instance->exports());
}

wasm_instance_t* wasm_frame_instance(const wasm_frame_t* frame) {
  return hide_instance(reveal_frame(frame)->instance());
}

#undef WASM_DEFINE_OWN
#undef WASM_DEFINE_VEC_BASE
#undef WASM_DEFINE_VEC_PLAIN
#undef WASM_DEFINE_VEC_OWN
#undef WASM_DEFINE_TYPE
#undef WASM_DEFINE_REF_BASE
#undef WASM_DEFINE_REF
#undef WASM_DEFINE_SHARABLE_REF

}  // extern "C"

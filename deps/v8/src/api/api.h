// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_API_H_
#define V8_API_API_H_

#include <memory>

#include "include/v8-container.h"
#include "include/v8-external.h"
#include "include/v8-function-callback.h"
#include "include/v8-proxy.h"
#include "include/v8-typed-array.h"
#include "include/v8-wasm.h"
#include "src/base/contextual.h"
#include "src/execution/isolate.h"
#include "src/objects/bigint.h"
#include "src/objects/contexts.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/js-collection.h"
#include "src/objects/js-generator.h"
#include "src/objects/js-promise.h"
#include "src/objects/js-proxy.h"
#include "src/objects/objects.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/source-text-module.h"
#include "src/objects/templates.h"
#include "src/utils/detachable-vector.h"

namespace v8 {

class DictionaryTemplate;
class Extension;
class Signature;
class Template;

namespace internal {
class JSArrayBufferView;
class JSFinalizationRegistry;
}  // namespace internal

namespace debug {
class AccessorPair;
class GeneratorObject;
class ScriptSource;
class Script;
class EphemeronTable;
}  // namespace debug

template <typename T, internal::ExternalPointerTag tag>
inline T ToCData(i::Isolate* isolate,
                 v8::internal::Tagged<v8::internal::Object> obj);
template <internal::ExternalPointerTag tag>
inline v8::internal::Address ToCData(
    v8::internal::Isolate* isolate,
    v8::internal::Tagged<v8::internal::Object> obj);

template <internal::ExternalPointerTag tag, typename T>
inline v8::internal::Handle<
    v8::internal::UnionOf<v8::internal::Smi, v8::internal::Foreign>>
FromCData(v8::internal::Isolate* isolate, T obj);

template <internal::ExternalPointerTag tag>
inline v8::internal::Handle<
    v8::internal::UnionOf<v8::internal::Smi, v8::internal::Foreign>>
FromCData(v8::internal::Isolate* isolate, v8::internal::Address obj);

class ApiFunction {
 public:
  explicit ApiFunction(v8::internal::Address addr) : addr_(addr) {}
  v8::internal::Address address() { return addr_; }

 private:
  v8::internal::Address addr_;
};

class RegisteredExtension {
 public:
  static void Register(std::unique_ptr<Extension>);
  static void UnregisterAll();
  Extension* extension() const { return extension_.get(); }
  RegisteredExtension* next() const { return next_; }
  static RegisteredExtension* first_extension() { return first_extension_; }

 private:
  explicit RegisteredExtension(Extension*);
  explicit RegisteredExtension(std::unique_ptr<Extension>);
  std::unique_ptr<Extension> extension_;
  RegisteredExtension* next_ = nullptr;
  static RegisteredExtension* first_extension_;
};

#define TO_LOCAL_LIST(V)                                 \
  V(ToLocal, AccessorPair, debug::AccessorPair)          \
  V(ToLocal, NativeContext, Context)                     \
  V(ToLocal, Object, Value)                              \
  V(ToLocal, Module, Module)                             \
  V(ToLocal, Name, Name)                                 \
  V(ToLocal, String, String)                             \
  V(ToLocal, Symbol, Symbol)                             \
  V(ToLocal, JSRegExp, RegExp)                           \
  V(ToLocal, JSReceiver, Object)                         \
  V(ToLocal, JSObject, Object)                           \
  V(ToLocal, JSFunction, Function)                       \
  V(ToLocal, JSArray, Array)                             \
  V(ToLocal, JSMap, Map)                                 \
  V(ToLocal, JSSet, Set)                                 \
  V(ToLocal, JSProxy, Proxy)                             \
  V(ToLocal, JSArrayBuffer, ArrayBuffer)                 \
  V(ToLocal, JSArrayBufferView, ArrayBufferView)         \
  V(ToLocal, JSDataView, DataView)                       \
  V(ToLocal, JSRabGsabDataView, DataView)                \
  V(ToLocal, JSTypedArray, TypedArray)                   \
  V(ToLocalShared, JSArrayBuffer, SharedArrayBuffer)     \
  V(ToLocal, FunctionTemplateInfo, FunctionTemplate)     \
  V(ToLocal, ObjectTemplateInfo, ObjectTemplate)         \
  V(ToLocal, DictionaryTemplateInfo, DictionaryTemplate) \
  V(SignatureToLocal, FunctionTemplateInfo, Signature)   \
  V(MessageToLocal, Object, Message)                     \
  V(PromiseToLocal, JSObject, Promise)                   \
  V(StackTraceToLocal, FixedArray, StackTrace)           \
  V(StackFrameToLocal, StackFrameInfo, StackFrame)       \
  V(NumberToLocal, Object, Number)                       \
  V(IntegerToLocal, Object, Integer)                     \
  V(Uint32ToLocal, Object, Uint32)                       \
  V(ToLocal, BigInt, BigInt)                             \
  V(ExternalToLocal, JSObject, External)                 \
  V(CallableToLocal, JSReceiver, Function)               \
  V(ToLocalPrimitive, Object, Primitive)                 \
  V(FixedArrayToLocal, FixedArray, FixedArray)           \
  V(PrimitiveArrayToLocal, FixedArray, PrimitiveArray)   \
  V(ToLocal, ScriptOrModule, ScriptOrModule)             \
  IF_WASM(V, ToLocal, WasmModuleObject, WasmModuleObject)

#define TO_LOCAL_NAME_LIST(V) \
  V(ToLocal)                  \
  V(ToLocalShared)            \
  V(SignatureToLocal)         \
  V(MessageToLocal)           \
  V(PromiseToLocal)           \
  V(StackTraceToLocal)        \
  V(StackFrameToLocal)        \
  V(NumberToLocal)            \
  V(IntegerToLocal)           \
  V(Uint32ToLocal)            \
  V(ExternalToLocal)          \
  V(CallableToLocal)          \
  V(ToLocalPrimitive)         \
  V(FixedArrayToLocal)        \
  V(PrimitiveArrayToLocal)

#define OPEN_HANDLE_LIST(V)                     \
  V(Template, TemplateInfo)                     \
  V(FunctionTemplate, FunctionTemplateInfo)     \
  V(ObjectTemplate, ObjectTemplateInfo)         \
  V(DictionaryTemplate, DictionaryTemplateInfo) \
  V(Signature, FunctionTemplateInfo)            \
  V(Data, Object)                               \
  V(Number, Number)                             \
  V(RegExp, JSRegExp)                           \
  V(Object, JSReceiver)                         \
  V(Array, JSArray)                             \
  V(Map, JSMap)                                 \
  V(Set, JSSet)                                 \
  V(ArrayBuffer, JSArrayBuffer)                 \
  V(ArrayBufferView, JSArrayBufferView)         \
  V(TypedArray, JSTypedArray)                   \
  V(Uint8Array, JSTypedArray)                   \
  V(Uint8ClampedArray, JSTypedArray)            \
  V(Int8Array, JSTypedArray)                    \
  V(Uint16Array, JSTypedArray)                  \
  V(Int16Array, JSTypedArray)                   \
  V(Uint32Array, JSTypedArray)                  \
  V(Int32Array, JSTypedArray)                   \
  V(Float16Array, JSTypedArray)                 \
  V(Float32Array, JSTypedArray)                 \
  V(Float64Array, JSTypedArray)                 \
  V(DataView, JSDataViewOrRabGsabDataView)      \
  V(SharedArrayBuffer, JSArrayBuffer)           \
  V(Name, Name)                                 \
  V(String, String)                             \
  V(Symbol, Symbol)                             \
  V(Script, JSFunction)                         \
  V(UnboundModuleScript, SharedFunctionInfo)    \
  V(UnboundScript, SharedFunctionInfo)          \
  V(Module, Module)                             \
  V(Function, JSReceiver)                       \
  V(CompileHintsCollector, Script)              \
  V(Message, JSMessageObject)                   \
  V(Context, NativeContext)                     \
  V(External, Object)                           \
  V(StackTrace, FixedArray)                     \
  V(StackFrame, StackFrameInfo)                 \
  V(Proxy, JSProxy)                             \
  V(debug::GeneratorObject, JSGeneratorObject)  \
  V(debug::ScriptSource, HeapObject)            \
  V(debug::Script, Script)                      \
  V(debug::EphemeronTable, EphemeronHashTable)  \
  V(debug::AccessorPair, AccessorPair)          \
  V(Promise, JSPromise)                         \
  V(Primitive, Object)                          \
  V(PrimitiveArray, FixedArray)                 \
  V(BigInt, BigInt)                             \
  V(ScriptOrModule, ScriptOrModule)             \
  V(FixedArray, FixedArray)                     \
  V(ModuleRequest, ModuleRequest)               \
  IF_WASM(V, WasmMemoryObject, WasmMemoryObject)

class Utils {
 public:
  static V8_INLINE bool ApiCheck(bool condition, const char* location,
                                 const char* message) {
    if (V8_UNLIKELY(!condition)) {
      Utils::ReportApiFailure(location, message);
    }
    return condition;
  }
  static void ReportOOMFailure(v8::internal::Isolate* isolate,
                               const char* location, const OOMDetails& details);

  // TODO(42203211): It would be nice if we could keep only a version with
  // direct handles. But the implicit conversion from handles to direct handles
  // combined with the heterogeneous copy constructor for direct handles make
  // this ambiguous.
  // TODO(42203211): Use C++20 concepts instead of the enable_if trait, when
  // they are fully supported in V8.
#define DECLARE_TO_LOCAL(Name)                                     \
  template <template <typename T> typename HandleType, typename T, \
            typename = std::enable_if_t<std::is_convertible_v<     \
                HandleType<T>, v8::internal::DirectHandle<T>>>>    \
  static inline auto Name(HandleType<T> obj);

  TO_LOCAL_NAME_LIST(DECLARE_TO_LOCAL)

#define DECLARE_TO_LOCAL_TYPED_ARRAY(Type, typeName, TYPE, ctype) \
  static inline Local<v8::Type##Array> ToLocal##Type##Array(      \
      v8::internal::DirectHandle<v8::internal::JSTypedArray> obj);

  TYPED_ARRAYS(DECLARE_TO_LOCAL_TYPED_ARRAY)

#define DECLARE_OPEN_HANDLE(From, To)                                          \
  static inline v8::internal::Handle<v8::internal::To> OpenHandle(             \
      const From* that, bool allow_empty_handle = false);                      \
  static inline v8::internal::DirectHandle<v8::internal::To> OpenDirectHandle( \
      const From* that, bool allow_empty_handle = false);                      \
  static inline v8::internal::IndirectHandle<v8::internal::To>                 \
  OpenIndirectHandle(const From* that, bool allow_empty_handle = false);

  OPEN_HANDLE_LIST(DECLARE_OPEN_HANDLE)

#undef DECLARE_OPEN_HANDLE
#undef DECLARE_TO_LOCAL_TYPED_ARRAY
#undef DECLARE_TO_LOCAL

  template <class From, class To>
  static inline Local<To> Convert(v8::internal::DirectHandle<From> obj);

  template <class T>
  static inline v8::internal::Handle<v8::internal::Object> OpenPersistent(
      const v8::PersistentBase<T>& persistent) {
    return v8::internal::Handle<v8::internal::Object>(persistent.slot());
  }

  template <class T>
  static inline v8::internal::Handle<v8::internal::Object> OpenPersistent(
      v8::Persistent<T>* persistent) {
    return OpenPersistent(*persistent);
  }

  template <class From, class To>
  static inline v8::internal::Handle<To> OpenHandle(v8::Local<From> handle) {
    return OpenHandle(*handle);
  }

  template <class From, class To>
  static inline v8::internal::DirectHandle<To> OpenDirectHandle(
      v8::Local<From> handle) {
    return OpenDirectHandle(*handle);
  }

 private:
  V8_NOINLINE V8_PRESERVE_MOST static void ReportApiFailure(
      const char* location, const char* message);

#define DECLARE_TO_LOCAL_PRIVATE(Name, From, To) \
  static inline Local<v8::To> Name##_helper(     \
      v8::internal::DirectHandle<v8::internal::From> obj);

  TO_LOCAL_LIST(DECLARE_TO_LOCAL_PRIVATE)
#undef DECLARE_TO_LOCAL_PRIVATE
};

template <class T>
inline T* ToApi(v8::internal::Handle<v8::internal::Object> obj) {
  return reinterpret_cast<T*>(obj.location());
}

template <class T>
inline v8::Local<T> ToApiHandle(
    v8::internal::DirectHandle<v8::internal::Object> obj) {
  return Utils::Convert<v8::internal::Object, T>(obj);
}

template <class T>
inline bool ToLocal(v8::internal::MaybeHandle<v8::internal::Object> maybe,
                    Local<T>* local) {
  v8::internal::Handle<v8::internal::Object> handle;
  if (maybe.ToHandle(&handle)) {
    *local = Utils::Convert<v8::internal::Object, T>(handle);
    return true;
  }
  return false;
}

namespace internal {

class PersistentHandles;

// This class is here in order to be able to declare it a friend of
// HandleScope.  Moving these methods to be members of HandleScope would be
// neat in some ways, but it would expose internal implementation details in
// our public header file, which is undesirable.
//
// An isolate has a single instance of this class to hold the current thread's
// data. In multithreaded V8 programs this data is copied in and out of storage
// so that the currently executing thread always has its own copy of this
// data.
class HandleScopeImplementer {
 public:
  class V8_NODISCARD EnteredContextRewindScope {
   public:
    explicit EnteredContextRewindScope(HandleScopeImplementer* hsi)
        : hsi_(hsi), saved_entered_context_count_(hsi->EnteredContextCount()) {}

    ~EnteredContextRewindScope() {
      DCHECK_LE(saved_entered_context_count_, hsi_->EnteredContextCount());
      while (saved_entered_context_count_ < hsi_->EnteredContextCount())
        hsi_->LeaveContext();
    }

   private:
    HandleScopeImplementer* hsi_;
    size_t saved_entered_context_count_;
  };

  explicit HandleScopeImplementer(Isolate* isolate)
      : isolate_(isolate),
        spare_(nullptr),
        last_handle_before_deferred_block_(nullptr) {}

  ~HandleScopeImplementer() { DeleteArray(spare_); }

  HandleScopeImplementer(const HandleScopeImplementer&) = delete;
  HandleScopeImplementer& operator=(const HandleScopeImplementer&) = delete;

  // Threading support for handle data.
  static int ArchiveSpacePerThread();
  char* RestoreThread(char* from);
  char* ArchiveThread(char* to);
  void FreeThreadResources();

  // Garbage collection support.
  V8_EXPORT_PRIVATE void Iterate(v8::internal::RootVisitor* v);
  V8_EXPORT_PRIVATE static char* Iterate(v8::internal::RootVisitor* v,
                                         char* data);

  inline internal::Address* GetSpareOrNewBlock();
  inline void DeleteExtensions(internal::Address* prev_limit);

  inline void EnterContext(Tagged<NativeContext> context);
  inline void LeaveContext();
  inline bool LastEnteredContextWas(Tagged<NativeContext> context);
  inline size_t EnteredContextCount() const { return entered_contexts_.size(); }

  // Returns the last entered context or an empty handle if no
  // contexts have been entered.
  inline Handle<NativeContext> LastEnteredContext();

  inline void SaveContext(Tagged<Context> context);
  inline Tagged<Context> RestoreContext();
  inline bool HasSavedContexts();

  inline DetachableVector<Address*>* blocks() { return &blocks_; }
  Isolate* isolate() const { return isolate_; }

  void ReturnBlock(Address* block) {
    DCHECK_NOT_NULL(block);
    if (spare_ != nullptr) DeleteArray(spare_);
    spare_ = block;
  }

  static const size_t kEnteredContextsOffset;

 private:
  void ResetAfterArchive() {
    blocks_.detach();
    entered_contexts_.detach();
    saved_contexts_.detach();
    spare_ = nullptr;
    last_handle_before_deferred_block_ = nullptr;
  }

  void Free() {
    DCHECK(blocks_.empty());
    DCHECK(entered_contexts_.empty());
    DCHECK(saved_contexts_.empty());

    blocks_.free();
    entered_contexts_.free();
    saved_contexts_.free();
    if (spare_ != nullptr) {
      DeleteArray(spare_);
      spare_ = nullptr;
    }
    DCHECK(isolate_->thread_local_top()->CallDepthIsZero());
  }

  void BeginDeferredScope();
  std::unique_ptr<PersistentHandles> DetachPersistent(Address* first_block);

  Isolate* isolate_;
  DetachableVector<Address*> blocks_;

  // Used as a stack to keep track of entered contexts.
  DetachableVector<Tagged<NativeContext>> entered_contexts_;

  // Used as a stack to keep track of saved contexts.
  DetachableVector<Tagged<Context>> saved_contexts_;
  Address* spare_;
  Address* last_handle_before_deferred_block_;
  // This is only used for threading support.
  HandleScopeData handle_scope_data_;

  void IterateThis(RootVisitor* v);
  char* RestoreThreadHelper(char* from);
  char* ArchiveThreadHelper(char* to);

  friend class HandleScopeImplementerOffsets;
  friend class PersistentHandlesScope;
};

const int kHandleBlockSize = v8::internal::KB - 2;  // fit in one page

void HandleScopeImplementer::SaveContext(Tagged<Context> context) {
  saved_contexts_.push_back(context);
}

Tagged<Context> HandleScopeImplementer::RestoreContext() {
  Tagged<Context> last_context = saved_contexts_.back();
  saved_contexts_.pop_back();
  return last_context;
}

bool HandleScopeImplementer::HasSavedContexts() {
  return !saved_contexts_.empty();
}

void HandleScopeImplementer::LeaveContext() {
  DCHECK(!entered_contexts_.empty());
  entered_contexts_.pop_back();
}

bool HandleScopeImplementer::LastEnteredContextWas(
    Tagged<NativeContext> context) {
  return !entered_contexts_.empty() && entered_contexts_.back() == context;
}

// If there's a spare block, use it for growing the current scope.
internal::Address* HandleScopeImplementer::GetSpareOrNewBlock() {
  internal::Address* block =
      (spare_ != nullptr) ? spare_
                          : NewArray<internal::Address>(kHandleBlockSize);
  spare_ = nullptr;
  return block;
}

void HandleScopeImplementer::DeleteExtensions(internal::Address* prev_limit) {
  while (!blocks_.empty()) {
    internal::Address* block_start = blocks_.back();
    internal::Address* block_limit = block_start + kHandleBlockSize;

    // SealHandleScope may make the prev_limit to point inside the block.
    // Cast possibly-unrelated pointers to plain Addres before comparing them
    // to avoid undefined behavior.
    if (reinterpret_cast<Address>(block_start) <
            reinterpret_cast<Address>(prev_limit) &&
        reinterpret_cast<Address>(prev_limit) <=
            reinterpret_cast<Address>(block_limit)) {
#ifdef ENABLE_HANDLE_ZAPPING
      internal::HandleScope::ZapRange(prev_limit, block_limit);
#endif
      break;
    }

    blocks_.pop_back();
#ifdef ENABLE_HANDLE_ZAPPING
    internal::HandleScope::ZapRange(block_start, block_limit);
#endif
    if (spare_ != nullptr) {
      DeleteArray(spare_);
    }
    spare_ = block_start;
  }
  DCHECK((blocks_.empty() && prev_limit == nullptr) ||
         (!blocks_.empty() && prev_limit != nullptr));
}

// This is a wrapper function called from CallApiGetter builtin when profiling
// or side-effect checking is enabled. It's supposed to set up the runtime
// call stats scope and check if the getter has side-effects in case debugger
// enabled the side-effects checking mode.
// It gets additional argument, the AccessorInfo object, via
// IsolateData::api_callback_thunk_argument slot.
void InvokeAccessorGetterCallback(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info);

// This is a wrapper function called from CallApiCallback builtin when profiling
// or side-effect checking is enabled. It's supposed to set up the runtime
// call stats scope and check if the callback has side-effects in case debugger
// enabled the side-effects checking mode.
// It gets additional argument, the v8::FunctionCallback address, via
// IsolateData::api_callback_thunk_argument slot.
void InvokeFunctionCallbackGeneric(
    const v8::FunctionCallbackInfo<v8::Value>& info);
void InvokeFunctionCallbackOptimized(
    const v8::FunctionCallbackInfo<v8::Value>& info);

void InvokeFinalizationRegistryCleanupFromTask(
    Handle<NativeContext> native_context,
    Handle<JSFinalizationRegistry> finalization_registry,
    Handle<Object> callback);

template <typename T>
EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
T ConvertDouble(double d);

template <typename T>
EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
bool ValidateCallbackInfo(const FunctionCallbackInfo<T>& info);

template <typename T>
EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
bool ValidateCallbackInfo(const PropertyCallbackInfo<T>& info);

DECLARE_CONTEXTUAL_VARIABLE_WITH_DEFAULT(StackAllocatedCheck, const bool, true);

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_H_

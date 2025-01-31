#ifndef SRC_NODE_CONTEXTIFY_H_
#define SRC_NODE_CONTEXTIFY_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object-inl.h"
#include "cppgc_helpers.h"
#include "node_context_data.h"
#include "node_errors.h"

namespace node {
class ExternalReferenceRegistry;

namespace contextify {

struct ContextOptions {
  v8::Local<v8::String> name;
  v8::Local<v8::String> origin;
  v8::Local<v8::Boolean> allow_code_gen_strings;
  v8::Local<v8::Boolean> allow_code_gen_wasm;
  std::unique_ptr<v8::MicrotaskQueue> own_microtask_queue;
  v8::Local<v8::Symbol> host_defined_options_id;
  bool vanilla = false;
};

/**
 * The memory management of a vm context is as follows:
 *
 *                                                          user code
 *                                                              │
 *                          As global proxy or                  ▼
 *     ┌──────────────┐  kSandboxObject embedder data   ┌────────────────┐
 * ┌─► │  V8 Context  │────────────────────────────────►│ Wrapper holder │
 * │   └──────────────┘                                 └───────┬────────┘
 * │         ▲  Object constructor/creation context             │
 * │         │                                                  │
 * │  ┌──────┴────────────┐  contextify_context_private_symbol  │
 * │  │ ContextifyContext │◄────────────────────────────────────┘
 * │  │   JS Wrapper      │◄──────────► ┌─────────────────────────┐
 * │  └───────────────────┘  cppgc      │ node::ContextifyContext │
 * │                                    │       C++ Object        │
 * └──────────────────────────────────► └─────────────────────────┘
 *     v8::TracedReference / ContextEmbedderIndex::kContextifyContext
 *
 * There are two possibilities for the "wrapper holder":
 *
 * 1. When vm.constants.DONT_CONTEXTIFY is used, the wrapper holder is the V8
 *    context's global proxy object
 * 2. Otherwise it's the arbitrary "sandbox object" that users pass into
 *    vm.createContext() or a new empty object created internally if they pass
 *    undefined.
 *
 * In 2, the global object of the new V8 context is created using
 * global_object_template with interceptors that perform any requested
 * operations on the global object in the context first on the sandbox object
 * living outside of the new context, then fall back to the global proxy of the
 * new context.
 *
 * It's critical for the user-accessible wrapper holder to keep the
 * ContextifyContext wrapper alive via contextify_context_private_symbol
 * so that the V8 context is always available to the user while they still
 * hold the vm "context" object alive.
 *
 * It's also critical for the V8 context to keep the wrapper holder
 * (specifically, the "sandbox object" if users pass one) as well as the
 * node::ContextifyContext C++ object alive, so that when the code
 * runs inside the object and accesses the global object, the interceptors
 * can still access the "sandbox object" and perform operations
 * on them, even if users already relinquish access to the outer
 * "sandbox object".
 *
 * The v8::TracedReference and the ContextEmbedderIndex::kContextifyContext
 * slot in the context only act as shortcuts between
 * the node::ContextifyContext C++ object and the V8 context.
 */
class ContextifyContext final : CPPGC_MIXIN(ContextifyContext) {
 public:
  SET_CPPGC_NAME(ContextifyContext)
  void Trace(cppgc::Visitor* visitor) const final;

  ContextifyContext(Environment* env,
                    v8::Local<v8::Object> wrapper,
                    v8::Local<v8::Context> v8_context,
                    ContextOptions* options);

  // The destructors don't need to do anything because when the wrapper is
  // going away, the context is already going away or otherwise it would've
  // been holding the wrapper alive, so there's no need to reset the pointers
  // in the context. Also, any global handles to the context would've been
  // empty at this point, and the per-Environment context tracking code is
  // capable of dealing with empty handles from contexts purged elsewhere.
  ~ContextifyContext() = default;

  static v8::MaybeLocal<v8::Context> CreateV8Context(
      v8::Isolate* isolate,
      v8::Local<v8::ObjectTemplate> object_template,
      const SnapshotData* snapshot_data,
      v8::MicrotaskQueue* queue);
  static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                         v8::Local<v8::ObjectTemplate> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static ContextifyContext* ContextFromContextifiedSandbox(
      Environment* env, const v8::Local<v8::Object>& wrapper_holder);

  inline v8::Local<v8::Context> context() const {
    return context_.Get(env()->isolate());
  }

  inline v8::Local<v8::Object> global_proxy() const {
    return context()->Global();
  }

  inline v8::Local<v8::Object> sandbox() const {
    // Only vanilla contexts have undefined sandboxes. sandbox() is only used by
    // interceptors who are not supposed to be called on vanilla contexts.
    v8::Local<v8::Value> result =
        context()->GetEmbedderData(ContextEmbedderIndex::kSandboxObject);
    CHECK(!result->IsUndefined());
    return result.As<v8::Object>();
  }

  inline v8::MicrotaskQueue* microtask_queue() const {
    return microtask_queue_.get();
  }

  template <typename T>
  static ContextifyContext* Get(const v8::PropertyCallbackInfo<T>& args);
  static ContextifyContext* Get(v8::Local<v8::Object> object);

  static void InitializeGlobalTemplates(IsolateData* isolate_data);

 private:
  static ContextifyContext* New(Environment* env,
                                v8::Local<v8::Object> sandbox_obj,
                                ContextOptions* options);
  // Initialize a context created from CreateV8Context()
  static ContextifyContext* New(v8::Local<v8::Context> ctx,
                                Environment* env,
                                v8::Local<v8::Object> sandbox_obj,
                                ContextOptions* options);

  static bool IsStillInitializing(const ContextifyContext* ctx);
  static void MakeContext(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void IsContext(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void CompileFunction(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static v8::Local<v8::Object> CompileFunctionAndCacheResult(
      Environment* env,
      v8::Local<v8::Context> parsing_context,
      v8::ScriptCompiler::Source* source,
      std::vector<v8::Local<v8::String>> params,
      std::vector<v8::Local<v8::Object>> context_extensions,
      v8::ScriptCompiler::CompileOptions options,
      bool produce_cached_data,
      v8::Local<v8::Symbol> id_symbol,
      const errors::TryCatchScope& try_catch);
  static v8::Intercepted PropertyQueryCallback(
      v8::Local<v8::Name> property,
      const v8::PropertyCallbackInfo<v8::Integer>& args);
  static v8::Intercepted PropertyGetterCallback(
      v8::Local<v8::Name> property,
      const v8::PropertyCallbackInfo<v8::Value>& args);
  static v8::Intercepted PropertySetterCallback(
      v8::Local<v8::Name> property,
      v8::Local<v8::Value> value,
      const v8::PropertyCallbackInfo<void>& args);
  static v8::Intercepted PropertyDescriptorCallback(
      v8::Local<v8::Name> property,
      const v8::PropertyCallbackInfo<v8::Value>& args);
  static v8::Intercepted PropertyDefinerCallback(
      v8::Local<v8::Name> property,
      const v8::PropertyDescriptor& desc,
      const v8::PropertyCallbackInfo<void>& args);
  static v8::Intercepted PropertyDeleterCallback(
      v8::Local<v8::Name> property,
      const v8::PropertyCallbackInfo<v8::Boolean>& args);
  static void PropertyEnumeratorCallback(
      const v8::PropertyCallbackInfo<v8::Array>& args);
  static v8::Intercepted IndexedPropertyQueryCallback(
      uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& args);
  static v8::Intercepted IndexedPropertyGetterCallback(
      uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& args);
  static v8::Intercepted IndexedPropertySetterCallback(
      uint32_t index,
      v8::Local<v8::Value> value,
      const v8::PropertyCallbackInfo<void>& args);
  static v8::Intercepted IndexedPropertyDescriptorCallback(
      uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& args);
  static v8::Intercepted IndexedPropertyDefinerCallback(
      uint32_t index,
      const v8::PropertyDescriptor& desc,
      const v8::PropertyCallbackInfo<void>& args);
  static v8::Intercepted IndexedPropertyDeleterCallback(
      uint32_t index, const v8::PropertyCallbackInfo<v8::Boolean>& args);
  static void IndexedPropertyEnumeratorCallback(
      const v8::PropertyCallbackInfo<v8::Array>& args);

  v8::TracedReference<v8::Context> context_;
  std::unique_ptr<v8::MicrotaskQueue> microtask_queue_;
};

class ContextifyScript final : CPPGC_MIXIN(ContextifyScript) {
 public:
  SET_CPPGC_NAME(ContextifyScript)
  void Trace(cppgc::Visitor* visitor) const final;

  ContextifyScript(Environment* env, v8::Local<v8::Object> object);
  ~ContextifyScript() override;

  v8::Local<v8::UnboundScript> unbound_script() const;
  void set_unbound_script(v8::Local<v8::UnboundScript>);

  static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                         v8::Local<v8::ObjectTemplate> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
  static ContextifyScript* New(Environment* env, v8::Local<v8::Object> object);
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static bool InstanceOf(Environment* env, const v8::Local<v8::Value>& args);
  static void CreateCachedData(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void RunInContext(const v8::FunctionCallbackInfo<v8::Value>& args);
  static bool EvalMachine(v8::Local<v8::Context> context,
                          Environment* env,
                          const int64_t timeout,
                          const bool display_errors,
                          const bool break_on_sigint,
                          const bool break_on_first_line,
                          v8::MicrotaskQueue* microtask_queue,
                          const v8::FunctionCallbackInfo<v8::Value>& args);

 private:
  v8::TracedReference<v8::UnboundScript> script_;
};

v8::Maybe<void> StoreCodeCacheResult(
    Environment* env,
    v8::Local<v8::Object> target,
    v8::ScriptCompiler::CompileOptions compile_options,
    const v8::ScriptCompiler::Source& source,
    bool produce_cached_data,
    std::unique_ptr<v8::ScriptCompiler::CachedData> new_cached_data);

v8::MaybeLocal<v8::Function> CompileFunction(
    v8::Local<v8::Context> context,
    v8::Local<v8::String> filename,
    v8::Local<v8::String> content,
    std::vector<v8::Local<v8::String>>* parameters);

}  // namespace contextify
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_CONTEXTIFY_H_

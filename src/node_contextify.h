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

class ContextifyContext : public BaseObject {
 public:
  ContextifyContext(Environment* env,
                    v8::Local<v8::Object> wrapper,
                    v8::Local<v8::Context> v8_context,
                    ContextOptions* options);
  ~ContextifyContext();

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(ContextifyContext)
  SET_SELF_SIZE(ContextifyContext)

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
    return PersistentToLocal::Default(env()->isolate(), context_);
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
  static BaseObjectPtr<ContextifyContext> New(Environment* env,
                                              v8::Local<v8::Object> sandbox_obj,
                                              ContextOptions* options);
  // Initialize a context created from CreateV8Context()
  static BaseObjectPtr<ContextifyContext> New(v8::Local<v8::Context> ctx,
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

  v8::Global<v8::Context> context_;
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

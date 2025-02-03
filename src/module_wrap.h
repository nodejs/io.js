#ifndef SRC_MODULE_WRAP_H_
#define SRC_MODULE_WRAP_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "base_object.h"
#include "v8-script.h"

namespace node {

class IsolateData;
class Environment;
class ExternalReferenceRegistry;

namespace contextify {
class ContextifyContext;
}

namespace loader {

enum ScriptType : int {
  kScript,
  kModule,
  kFunction,
};

enum HostDefinedOptions : int {
  kID = 8,
  kLength = 9,
};

class ModuleWrap : public BaseObject {
 public:
  enum InternalFields {
    kModuleSlot = BaseObject::kInternalFieldCount,
    kURLSlot,
    kSyntheticEvaluationStepsSlot,
    kContextObjectSlot,  // Object whose creation context is the target Context
    kInternalFieldCount
  };

  static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                         v8::Local<v8::ObjectTemplate> target);
  static void CreatePerContextProperties(v8::Local<v8::Object> target,
                                         v8::Local<v8::Value> unused,
                                         v8::Local<v8::Context> context,
                                         void* priv);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
  static void HostInitializeImportMetaObjectCallback(
      v8::Local<v8::Context> context,
      v8::Local<v8::Module> module,
      v8::Local<v8::Object> meta);

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackField("resolve_cache", resolve_cache_);
  }

  v8::Local<v8::Context> context() const;
  v8::Maybe<bool> CheckUnsettledTopLevelAwait();

  SET_MEMORY_INFO_NAME(ModuleWrap)
  SET_SELF_SIZE(ModuleWrap)

  bool IsNotIndicativeOfMemoryLeakAtExit() const override {
    // XXX: The garbage collection rules for ModuleWrap are *super* unclear.
    // Do these objects ever get GC'd? Are we just okay with leaking them?
    return true;
  }

  static v8::Local<v8::PrimitiveArray> GetHostDefinedOptions(
      v8::Isolate* isolate, v8::Local<v8::Symbol> symbol);

  // When user_cached_data is not std::nullopt, use the code cache if it's not
  // nullptr, otherwise don't use code cache.
  // TODO(joyeecheung): when it is std::nullopt, use on-disk cache
  // See: https://github.com/nodejs/node/issues/47472
  static v8::MaybeLocal<v8::Module> CompileSourceTextModule(
      Realm* realm,
      v8::Local<v8::String> source_text,
      v8::Local<v8::String> url,
      int line_offset,
      int column_offset,
      v8::Local<v8::PrimitiveArray> host_defined_options,
      std::optional<v8::ScriptCompiler::CachedData*> user_cached_data,
      bool* cache_rejected);

  static void CreateRequiredModuleFacade(
      const v8::FunctionCallbackInfo<v8::Value>& args);

 private:
  ModuleWrap(Realm* realm,
             v8::Local<v8::Object> object,
             v8::Local<v8::Module> module,
             v8::Local<v8::String> url,
             v8::Local<v8::Object> context_object,
             v8::Local<v8::Value> synthetic_evaluation_step);
  ~ModuleWrap() override;

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetModuleRequests(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void InstantiateSync(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EvaluateSync(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetNamespaceSync(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void Link(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Instantiate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Evaluate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetNamespace(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetStatus(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetError(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void SetImportModuleDynamicallyCallback(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetInitializeImportMetaObjectCallback(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static v8::MaybeLocal<v8::Value> SyntheticModuleEvaluationStepsCallback(
      v8::Local<v8::Context> context, v8::Local<v8::Module> module);
  static void SetSyntheticExport(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void CreateCachedData(const v8::FunctionCallbackInfo<v8::Value>& args);

  static v8::MaybeLocal<v8::Module> ResolveModuleCallback(
      v8::Local<v8::Context> context,
      v8::Local<v8::String> specifier,
      v8::Local<v8::FixedArray> import_attributes,
      v8::Local<v8::Module> referrer);
  static ModuleWrap* GetFromModule(node::Environment*, v8::Local<v8::Module>);

  v8::Global<v8::Module> module_;
  std::unordered_map<std::string, v8::Global<v8::Object>> resolve_cache_;
  contextify::ContextifyContext* contextify_context_ = nullptr;
  bool synthetic_ = false;
  int module_hash_;
};

}  // namespace loader
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_MODULE_WRAP_H_

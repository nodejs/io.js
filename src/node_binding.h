#ifndef SRC_NODE_BINDING_H_
#define SRC_NODE_BINDING_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#if defined(__POSIX__)
#include <dlfcn.h>
#endif

#include "node.h"
#define NAPI_EXPERIMENTAL
#include "node_api.h"
#include "uv.h"

enum {
  NM_F_BUILTIN = 1 << 0,  // Unused.
  NM_F_LINKED = 1 << 1,
  NM_F_INTERNAL = 1 << 2,
  NM_F_DELETEME = 1 << 3,
};

// Make sure our internal values match the public API's values.
static_assert(static_cast<int>(NM_F_LINKED) ==
                  static_cast<int>(node::ModuleFlags::kLinked),
              "NM_F_LINKED != node::ModuleFlags::kLinked");

#if NODE_HAVE_I18N_SUPPORT
#define NODE_BUILTIN_ICU_BINDINGS(V) V(icu)
#else
#define NODE_BUILTIN_ICU_BINDINGS(V)
#endif

#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#define NODE_BUILTIN_QUIC_BINDINGS(V) V(quic)
#else
#define NODE_BUILTIN_QUIC_BINDINGS(V)
#endif

#define NODE_BINDINGS_WITH_PER_ISOLATE_INIT(V)                                 \
  V(async_wrap)                                                                \
  V(blob)                                                                      \
  V(builtins)                                                                  \
  V(contextify)                                                                \
  V(encoding_binding)                                                          \
  V(fs)                                                                        \
  V(fs_dir)                                                                    \
  V(messaging)                                                                 \
  V(mksnapshot)                                                                \
  V(modules)                                                                   \
  V(module_wrap)                                                               \
  V(performance)                                                               \
  V(process_methods)                                                           \
  V(timers)                                                                    \
  V(url)                                                                       \
  V(worker)                                                                    \
  NODE_BUILTIN_ICU_BINDINGS(V)                                                 \
  NODE_BUILTIN_QUIC_BINDINGS(V)

#define NODE_BINDING_CONTEXT_AWARE_CPP(modname, regfunc, priv, flags)          \
  static node::node_module _module = {                                         \
      NODE_MODULE_VERSION,                                                     \
      flags,                                                                   \
      nullptr,                                                                 \
      __FILE__,                                                                \
      nullptr,                                                                 \
      (node::addon_context_register_func)(regfunc),                            \
      NODE_STRINGIFY(modname),                                                 \
      priv,                                                                    \
      nullptr};                                                                \
  void _register_##modname() { node_module_register(&_module); }

void napi_module_register_by_symbol(
    v8::Local<v8::Object> exports,
    v8::Local<v8::Value> module,
    v8::Local<v8::Context> context,
    napi_addon_register_func init,
    int32_t module_api_version = NODE_API_DEFAULT_MODULE_API_VERSION);

node::addon_context_register_func get_node_api_context_register_func(
    node::Environment* node_env,
    const char* module_name,
    int32_t module_api_version);

namespace node {

// Define a node internal binding that may be loaded in a context of
// a node::Environment.
// If an internal binding needs initializing per-isolate templates, define
// with NODE_BINDING_PER_ISOLATE_INIT too.
#define NODE_BINDING_CONTEXT_AWARE_INTERNAL(modname, regfunc)                  \
  NODE_BINDING_CONTEXT_AWARE_CPP(modname, regfunc, nullptr, NM_F_INTERNAL)

// Define a per-isolate initialization function for a node internal binding.
// The modname should be registered in the NODE_BINDINGS_WITH_PER_ISOLATE_INIT
// list.
#define NODE_BINDING_PER_ISOLATE_INIT(modname, per_isolate_func)               \
  void _register_isolate_##modname(node::IsolateData* isolate_data,            \
                                   v8::Local<v8::ObjectTemplate> target) {     \
    per_isolate_func(isolate_data, target);                                    \
  }

// Globals per process
// This is set by node::Init() which is used by embedders
extern bool node_is_initialized;

namespace binding {

class DLib {
 public:
#ifdef __POSIX__
  static const int kDefaultFlags = RTLD_LAZY;
#else
  static const int kDefaultFlags = 0;
#endif

  DLib(const char* filename, int flags);

  bool Open();
  void Close();
  void* GetSymbolAddress(const char* name);
  void SaveInGlobalHandleMap(node_module* mp);
  node_module* GetSavedModuleFromGlobalHandleMap();

  const std::string filename_;
  const int flags_;
  std::string errmsg_;
  void* handle_;
#ifndef __POSIX__
  uv_lib_t lib_;
#endif
  bool has_entry_in_global_handle_map_ = false;

  DLib(const DLib&) = delete;
  DLib& operator=(const DLib&) = delete;
};

// Call _register<module_name> functions for all of
// the built-in bindings. Because built-in bindings don't
// use the __attribute__((constructor)). Need to
// explicitly call the _register* functions.
void RegisterBuiltinBindings();
// Create per-isolate templates for the internal bindings.
void CreateInternalBindingTemplates(IsolateData* isolate_data);
void GetInternalBinding(const v8::FunctionCallbackInfo<v8::Value>& args);
void GetLinkedBinding(const v8::FunctionCallbackInfo<v8::Value>& args);
void DLOpen(const v8::FunctionCallbackInfo<v8::Value>& args);

}  // namespace binding

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_NODE_BINDING_H_

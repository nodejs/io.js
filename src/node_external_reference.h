#ifndef SRC_NODE_EXTERNAL_REFERENCE_H_
#define SRC_NODE_EXTERNAL_REFERENCE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cinttypes>
#include <vector>
#include "v8-fast-api-calls.h"
#include "v8.h"

namespace node {

using CFunctionCallbackWithOneByteString =
    uint32_t (*)(v8::Local<v8::Value>, const v8::FastOneByteString&);
using CFunctionCallback = void (*)(v8::Local<v8::Value> unused,
                                   v8::Local<v8::Value> receiver);
using CFunctionCallbackReturnDouble =
    double (*)(v8::Local<v8::Object> unused, v8::Local<v8::Object> receiver);
using CFunctionCallbackReturnInt32 =
    int32_t (*)(v8::Local<v8::Object> unused,
                v8::Local<v8::Object> receiver,
                const v8::FastOneByteString& input,
                // NOLINTNEXTLINE(runtime/references) This is V8 api.
                v8::FastApiCallbackOptions& options);
using CFunctionCallbackValueReturnDouble =
    double (*)(v8::Local<v8::Value> receiver);
using CFunctionCallbackValueReturnDoubleUnusedReceiver =
    double (*)(v8::Local<v8::Value> unused, v8::Local<v8::Value> receiver);
using CFunctionCallbackWithInt64 = void (*)(v8::Local<v8::Object> unused,
                                            v8::Local<v8::Object> receiver,
                                            int64_t);
using CFunctionCallbackWithBool = void (*)(v8::Local<v8::Object> unused,
                                           v8::Local<v8::Object> receiver,
                                           bool);
using CFunctionCallbackWithString =
    bool (*)(v8::Local<v8::Value>, const v8::FastOneByteString& input);
using CFunctionCallbackWithStrings =
    bool (*)(v8::Local<v8::Value>,
             const v8::FastOneByteString& input,
             const v8::FastOneByteString& base);
using CFunctionCallbackWithTwoUint8Arrays =
    int32_t (*)(v8::Local<v8::Value>,
                const v8::FastApiTypedArray<uint8_t>&,
                const v8::FastApiTypedArray<uint8_t>&);
using CFunctionCallbackWithTwoUint8ArraysFallback =
    bool (*)(v8::Local<v8::Value>,
             const v8::FastApiTypedArray<uint8_t>&,
             const v8::FastApiTypedArray<uint8_t>&,
             v8::FastApiCallbackOptions&);
using CFunctionCallbackWithUint8ArrayUint32Int64Bool =
    int32_t (*)(v8::Local<v8::Value>,
                const v8::FastApiTypedArray<uint8_t>&,
                uint32_t,
                int64_t,
                bool);
using CFunctionWithUint32 = uint32_t (*)(v8::Local<v8::Value>,
                                         const uint32_t input);
using CFunctionWithDoubleReturnDouble = double (*)(v8::Local<v8::Value>,
                                                   v8::Local<v8::Value>,
                                                   const double);
using CFunctionWithInt64Fallback = void (*)(v8::Local<v8::Value>,
                                            v8::Local<v8::Value>,
                                            const int64_t,
                                            v8::FastApiCallbackOptions&);
using CFunctionWithBool = void (*)(v8::Local<v8::Value>,
                                   v8::Local<v8::Value>,
                                   bool);

using CFunctionWriteString =
    uint32_t (*)(v8::Local<v8::Value> receiver,
                 const v8::FastApiTypedArray<uint8_t>& dst,
                 const v8::FastOneByteString& src,
                 uint32_t offset,
                 uint32_t max_length);

using CFunctionBufferCopy =
    uint32_t (*)(v8::Local<v8::Value> receiver,
                 const v8::FastApiTypedArray<uint8_t>& source,
                 const v8::FastApiTypedArray<uint8_t>& target,
                 uint32_t target_start,
                 uint32_t source_start,
                 uint32_t to_copy);

// This class manages the external references from the V8 heap
// to the C++ addresses in Node.js.
class ExternalReferenceRegistry {
 public:
  ExternalReferenceRegistry();

#define ALLOWED_EXTERNAL_REFERENCE_TYPES(V)                                    \
  V(CFunctionCallback)                                                         \
  V(CFunctionCallbackWithOneByteString)                                        \
  V(CFunctionCallbackReturnDouble)                                             \
  V(CFunctionCallbackReturnInt32)                                              \
  V(CFunctionCallbackValueReturnDouble)                                        \
  V(CFunctionCallbackValueReturnDoubleUnusedReceiver)                          \
  V(CFunctionCallbackWithInt64)                                                \
  V(CFunctionCallbackWithBool)                                                 \
  V(CFunctionCallbackWithString)                                               \
  V(CFunctionCallbackWithStrings)                                              \
  V(CFunctionCallbackWithTwoUint8Arrays)                                       \
  V(CFunctionCallbackWithTwoUint8ArraysFallback)                               \
  V(CFunctionCallbackWithUint8ArrayUint32Int64Bool)                            \
  V(CFunctionWithUint32)                                                       \
  V(CFunctionWithDoubleReturnDouble)                                           \
  V(CFunctionWithInt64Fallback)                                                \
  V(CFunctionWithBool)                                                         \
  V(CFunctionBufferCopy)                                                       \
  V(CFunctionWriteString)                                                      \
  V(const v8::CFunctionInfo*)                                                  \
  V(v8::FunctionCallback)                                                      \
  V(v8::AccessorNameGetterCallback)                                            \
  V(v8::AccessorNameSetterCallback)                                            \
  V(v8::NamedPropertyGetterCallback)                                           \
  V(v8::NamedPropertyDefinerCallback)                                          \
  V(v8::NamedPropertyDeleterCallback)                                          \
  V(v8::NamedPropertyEnumeratorCallback)                                       \
  V(v8::NamedPropertyQueryCallback)                                            \
  V(v8::NamedPropertySetterCallback)                                           \
  V(v8::IndexedPropertyGetterCallbackV2)                                       \
  V(v8::IndexedPropertySetterCallbackV2)                                       \
  V(v8::IndexedPropertyDefinerCallbackV2)                                      \
  V(v8::IndexedPropertyDeleterCallbackV2)                                      \
  V(v8::IndexedPropertyQueryCallbackV2)                                        \
  V(const v8::String::ExternalStringResourceBase*)

#define V(ExternalReferenceType)                                               \
  void Register(ExternalReferenceType addr) { RegisterT(addr); }
  ALLOWED_EXTERNAL_REFERENCE_TYPES(V)
#undef V

  // This can be called only once.
  const std::vector<intptr_t>& external_references();

  bool is_empty() { return external_references_.empty(); }

 private:
  template <typename T>
  void RegisterT(T* address) {
    external_references_.push_back(reinterpret_cast<intptr_t>(address));
  }
  bool is_finalized_ = false;
  std::vector<intptr_t> external_references_;
};

#define EXTERNAL_REFERENCE_BINDING_LIST_BASE(V)                                \
  V(async_wrap)                                                                \
  V(binding)                                                                   \
  V(blob)                                                                      \
  V(buffer)                                                                    \
  V(builtins)                                                                  \
  V(cares_wrap)                                                                \
  V(config)                                                                    \
  V(contextify)                                                                \
  V(credentials)                                                               \
  V(encoding_binding)                                                          \
  V(env_var)                                                                   \
  V(errors)                                                                    \
  V(fs)                                                                        \
  V(fs_dir)                                                                    \
  V(fs_event_wrap)                                                             \
  V(handle_wrap)                                                               \
  V(heap_utils)                                                                \
  V(http_parser)                                                               \
  V(internal_only_v8)                                                          \
  V(messaging)                                                                 \
  V(mksnapshot)                                                                \
  V(module_wrap)                                                               \
  V(modules)                                                                   \
  V(options)                                                                   \
  V(os)                                                                        \
  V(performance)                                                               \
  V(permission)                                                                \
  V(process_methods)                                                           \
  V(process_object)                                                            \
  V(process_wrap)                                                              \
  V(report)                                                                    \
  V(task_queue)                                                                \
  V(tcp_wrap)                                                                  \
  V(tty_wrap)                                                                  \
  V(udp_wrap)                                                                  \
  V(url)                                                                       \
  V(util)                                                                      \
  V(pipe_wrap)                                                                 \
  V(sea)                                                                       \
  V(serdes)                                                                    \
  V(string_decoder)                                                            \
  V(stream_wrap)                                                               \
  V(signal_wrap)                                                               \
  V(spawn_sync)                                                                \
  V(trace_events)                                                              \
  V(timers)                                                                    \
  V(types)                                                                     \
  V(uv)                                                                        \
  V(v8)                                                                        \
  V(zlib)                                                                      \
  V(wasm_web_api)                                                              \
  V(worker)

#if NODE_HAVE_I18N_SUPPORT
#define EXTERNAL_REFERENCE_BINDING_LIST_I18N(V) V(icu)
#else
#define EXTERNAL_REFERENCE_BINDING_LIST_I18N(V)
#endif  // NODE_HAVE_I18N_SUPPORT

#if HAVE_INSPECTOR
#define EXTERNAL_REFERENCE_BINDING_LIST_INSPECTOR(V)                           \
  V(inspector)                                                                 \
  V(profiler)
#else
#define EXTERNAL_REFERENCE_BINDING_LIST_INSPECTOR(V)
#endif  // HAVE_INSPECTOR

#if HAVE_OPENSSL
#define EXTERNAL_REFERENCE_BINDING_LIST_CRYPTO(V) V(crypto) V(tls_wrap)
#else
#define EXTERNAL_REFERENCE_BINDING_LIST_CRYPTO(V)
#endif  // HAVE_OPENSSL

#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#define EXTERNAL_REFERENCE_BINDING_LIST_QUIC(V) V(quic)
#else
#define EXTERNAL_REFERENCE_BINDING_LIST_QUIC(V)
#endif

#define EXTERNAL_REFERENCE_BINDING_LIST(V)                                     \
  EXTERNAL_REFERENCE_BINDING_LIST_BASE(V)                                      \
  EXTERNAL_REFERENCE_BINDING_LIST_INSPECTOR(V)                                 \
  EXTERNAL_REFERENCE_BINDING_LIST_I18N(V)                                      \
  EXTERNAL_REFERENCE_BINDING_LIST_CRYPTO(V)                                    \
  EXTERNAL_REFERENCE_BINDING_LIST_QUIC(V)

}  // namespace node

// Declare all the external reference registration functions here,
// and define them later with #NODE_BINDING_EXTERNAL_REFERENCE(modname, func);
#define V(modname)                                                             \
  void _register_external_reference_##modname(                                 \
      node::ExternalReferenceRegistry* registry);
EXTERNAL_REFERENCE_BINDING_LIST(V)
#undef V

#define NODE_BINDING_EXTERNAL_REFERENCE(modname, func)                         \
  void _register_external_reference_##modname(                                 \
      node::ExternalReferenceRegistry* registry) {                             \
    func(registry);                                                            \
  }
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_NODE_EXTERNAL_REFERENCE_H_

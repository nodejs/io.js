// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_ASYNC_WRAP_H_
#define SRC_ASYNC_WRAP_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object.h"
#include "v8.h"

#include <cstdint>

namespace node {

#define NODE_ASYNC_NON_CRYPTO_PROVIDER_TYPES(V)                                \
  V(NONE)                                                                      \
  V(DIRHANDLE)                                                                 \
  V(DNSCHANNEL)                                                                \
  V(ELDHISTOGRAM)                                                              \
  V(FILEHANDLE)                                                                \
  V(FILEHANDLECLOSEREQ)                                                        \
  V(BLOBREADER)                                                                \
  V(FSEVENTWRAP)                                                               \
  V(FSREQCALLBACK)                                                             \
  V(FSREQPROMISE)                                                              \
  V(GETADDRINFOREQWRAP)                                                        \
  V(GETNAMEINFOREQWRAP)                                                        \
  V(HEAPSNAPSHOT)                                                              \
  V(HTTP2SESSION)                                                              \
  V(HTTP2STREAM)                                                               \
  V(HTTP2PING)                                                                 \
  V(HTTP2SETTINGS)                                                             \
  V(HTTPINCOMINGMESSAGE)                                                       \
  V(HTTPCLIENTREQUEST)                                                         \
  V(JSSTREAM)                                                                  \
  V(JSUDPWRAP)                                                                 \
  V(MESSAGEPORT)                                                               \
  V(PIPECONNECTWRAP)                                                           \
  V(PIPESERVERWRAP)                                                            \
  V(PIPEWRAP)                                                                  \
  V(PROCESSWRAP)                                                               \
  V(PROMISE)                                                                   \
  V(QUERYWRAP)                                                                 \
  V(QUIC_ENDPOINT)                                                             \
  V(QUIC_LOGSTREAM)                                                            \
  V(QUIC_PACKET)                                                               \
  V(QUIC_SESSION)                                                              \
  V(QUIC_STREAM)                                                               \
  V(QUIC_UDP)                                                                  \
  V(SHUTDOWNWRAP)                                                              \
  V(SIGNALWRAP)                                                                \
  V(STATWATCHER)                                                               \
  V(STREAMPIPE)                                                                \
  V(TCPCONNECTWRAP)                                                            \
  V(TCPSERVERWRAP)                                                             \
  V(TCPWRAP)                                                                   \
  V(TTYWRAP)                                                                   \
  V(UDPSENDWRAP)                                                               \
  V(UDPWRAP)                                                                   \
  V(SIGINTWATCHDOG)                                                            \
  V(WORKER)                                                                    \
  V(WORKERHEAPSNAPSHOT)                                                        \
  V(WRITEWRAP)                                                                 \
  V(ZLIB)

#if HAVE_OPENSSL
#define NODE_ASYNC_CRYPTO_PROVIDER_TYPES(V)                                   \
  V(CHECKPRIMEREQUEST)                                                        \
  V(PBKDF2REQUEST)                                                            \
  V(KEYPAIRGENREQUEST)                                                        \
  V(KEYGENREQUEST)                                                            \
  V(KEYEXPORTREQUEST)                                                         \
  V(CIPHERREQUEST)                                                            \
  V(DERIVEBITSREQUEST)                                                        \
  V(HASHREQUEST)                                                              \
  V(RANDOMBYTESREQUEST)                                                       \
  V(RANDOMPRIMEREQUEST)                                                       \
  V(SCRYPTREQUEST)                                                            \
  V(SIGNREQUEST)                                                              \
  V(TLSWRAP)                                                                  \
  V(VERIFYREQUEST)
#else
#define NODE_ASYNC_CRYPTO_PROVIDER_TYPES(V)
#endif  // HAVE_OPENSSL

#define NODE_ASYNC_PROVIDER_TYPES(V)                                           \
  NODE_ASYNC_NON_CRYPTO_PROVIDER_TYPES(V)                                      \
  NODE_ASYNC_CRYPTO_PROVIDER_TYPES(V)

class Environment;
class DestroyParam;
class ExternalReferenceRegistry;

class AsyncWrap : public BaseObject {
 public:
  enum ProviderType {
#define V(PROVIDER)                                                           \
    PROVIDER_ ## PROVIDER,
    NODE_ASYNC_PROVIDER_TYPES(V)
#undef V
    PROVIDERS_LENGTH,
  };

  AsyncWrap(Environment* env,
            v8::Local<v8::Object> object,
            ProviderType provider,
            double execution_async_id = kInvalidAsyncId);

  // This constructor creates a reusable instance where user is responsible
  // to call set_provider_type() and AsyncReset() before use.
  AsyncWrap(Environment* env, v8::Local<v8::Object> object);

  ~AsyncWrap() override;

  AsyncWrap() = delete;

  static constexpr double kInvalidAsyncId = -1;

  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      IsolateData* isolate_data);
  inline static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);

  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
  static void CreatePerContextProperties(v8::Local<v8::Object> target,
                                         v8::Local<v8::Value> unused,
                                         v8::Local<v8::Context> context,
                                         void* priv);
  static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                         v8::Local<v8::ObjectTemplate> target);

  static void GetAsyncId(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PushAsyncContext(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PopAsyncContext(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ExecutionAsyncResource(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ClearAsyncIdStack(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void AsyncReset(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetProviderType(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void QueueDestroyAsyncId(
    const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetCallbackTrampoline(
    const v8::FunctionCallbackInfo<v8::Value>& args);

  static void EmitAsyncInit(Environment* env,
                            v8::Local<v8::Object> object,
                            v8::Local<v8::String> type,
                            double async_id,
                            double trigger_async_id);

  static void EmitDestroy(Environment* env, double async_id);
  static void EmitBefore(Environment* env, double async_id);
  static void EmitAfter(Environment* env, double async_id);
  static void EmitPromiseResolve(Environment* env, double async_id);

  void EmitDestroy(bool from_gc = false);

  void EmitTraceEventBefore();
  static void EmitTraceEventAfter(ProviderType type, double async_id);
  void EmitTraceEventDestroy();

  static void DestroyAsyncIdsCallback(Environment* env);

  inline ProviderType provider_type() const;
  inline ProviderType set_provider_type(ProviderType provider);

  inline double get_async_id() const;
  inline double get_trigger_async_id() const;

  inline v8::Local<v8::Value> context_frame() const;

  void AsyncReset(v8::Local<v8::Object> resource,
                  double execution_async_id = kInvalidAsyncId);

  // Only call these within a valid HandleScope.
  v8::MaybeLocal<v8::Value> MakeCallback(const v8::Local<v8::Function> cb,
                                         int argc,
                                         v8::Local<v8::Value>* argv);
  inline v8::MaybeLocal<v8::Value> MakeCallback(
      const v8::Local<v8::Symbol> symbol,
      int argc,
      v8::Local<v8::Value>* argv);
  inline v8::MaybeLocal<v8::Value> MakeCallback(
      const v8::Local<v8::String> symbol,
      int argc,
      v8::Local<v8::Value>* argv);
  inline v8::MaybeLocal<v8::Value> MakeCallback(
      const v8::Local<v8::Name> symbol,
      int argc,
      v8::Local<v8::Value>* argv);

  virtual std::string diagnostic_name() const;
  const char* MemoryInfoName() const override;

  static void WeakCallback(const v8::WeakCallbackInfo<DestroyParam> &info);

  // Returns the object that 'owns' an async wrap. For example, for a
  // TCP connection handle, this is the corresponding net.Socket.
  v8::Local<v8::Object> GetOwner();
  static v8::Local<v8::Object> GetOwner(Environment* env,
                                        v8::Local<v8::Object> obj);

  bool IsDoneInitializing() const override;

 private:
  ProviderType provider_type_ = PROVIDER_NONE;
  bool init_hook_ran_ = false;
  // Because the values may be Reset(), cannot be made const.
  double async_id_ = kInvalidAsyncId;
  double trigger_async_id_ = kInvalidAsyncId;

  v8::Global<v8::Value> context_frame_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ASYNC_WRAP_H_

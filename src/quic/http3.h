#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <base_object.h>
#include <env.h>
#include <memory_tracker.h>
#include "session.h"

namespace node::quic {
// Provides an implementation of the HTTP/3 Application implementation
class Http3Application final : public Session::ApplicationProvider {
 public:
  static bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static void InitPerIsolate(IsolateData* isolate_data,
                             v8::Local<v8::ObjectTemplate> target);
  static void InitPerContext(Realm* realm, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  Http3Application(Environment* env,
                   v8::Local<v8::Object> object,
                   const Session::Application_Options& options);

  std::unique_ptr<Session::Application> Create(Session* session) override;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_SELF_SIZE(Http3Application)
  SET_MEMORY_INFO_NAME(Http3Application)

  std::string ToString() const;

 private:
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  Session::Application_Options options_;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

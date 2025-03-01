#ifndef SRC_NODE_PROCESS_H_
#define SRC_NODE_PROCESS_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_snapshotable.h"
#include "v8-fast-api-calls.h"
#include "v8.h"

namespace node {

class Environment;
class IsolateData;
class MemoryTracker;
class ExternalReferenceRegistry;
class Realm;

void CreateEnvProxyTemplate(IsolateData* isolate_data);

// Most of the time, it's best to use `console.error` to write
// to the process.stderr stream.  However, in some cases, such as
// when debugging the stream.Writable class or the process.nextTick
// function, it is useful to bypass JavaScript entirely.
void RawDebug(const v8::FunctionCallbackInfo<v8::Value>& args);

v8::MaybeLocal<v8::Value> ProcessEmit(Environment* env,
                                      std::string_view event,
                                      v8::Local<v8::Value> message);

v8::Maybe<bool> ProcessEmitWarningGeneric(Environment* env,
                                          std::string_view warning,
                                          std::string_view type = "",
                                          std::string_view code = "");

template <typename... Args>
inline v8::Maybe<bool> ProcessEmitWarning(Environment* env,
                                          const char* fmt,
                                          Args&&... args);

v8::Maybe<void> ProcessEmitWarningSync(Environment* env,
                                       std::string_view message);
v8::Maybe<bool> ProcessEmitExperimentalWarning(Environment* env,
                                               const std::string& warning);
v8::Maybe<bool> ProcessEmitDeprecationWarning(
    Environment* env,
    const std::string& warning,
    std::string_view deprecation_code);

v8::MaybeLocal<v8::Object> CreateProcessObject(Realm* env);
void PatchProcessObject(const v8::FunctionCallbackInfo<v8::Value>& args);

namespace process {
class BindingData : public SnapshotableObject {
 public:
  struct InternalFieldInfo : public node::InternalFieldInfoBase {
    AliasedBufferIndex hrtime_buffer;
  };

  static void AddMethods(v8::Isolate* isolate,
                         v8::Local<v8::ObjectTemplate> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  SERIALIZABLE_OBJECT_METHODS()
  SET_BINDING_ID(process_binding_data)

  BindingData(Realm* realm,
              v8::Local<v8::Object> object,
              InternalFieldInfo* info = nullptr);

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(BindingData)
  SET_SELF_SIZE(BindingData)

  static BindingData* FromV8Value(v8::Local<v8::Value> receiver);
  static void NumberImpl(BindingData* receiver);

  static void FastNumber(v8::Local<v8::Value> unused,
                         v8::Local<v8::Value> receiver) {
    NumberImpl(FromV8Value(receiver));
  }

  static void SlowNumber(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void BigIntImpl(BindingData* receiver);

  static void FastBigInt(v8::Local<v8::Value> unused,
                         v8::Local<v8::Value> receiver) {
    BigIntImpl(FromV8Value(receiver));
  }

  static void SlowBigInt(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void LoadEnvFile(const v8::FunctionCallbackInfo<v8::Value>& args);

 private:
  // Buffer length in uint32.
  static constexpr size_t kHrTimeBufferLength = 3;
  AliasedUint32Array hrtime_buffer_;
  InternalFieldInfo* internal_field_info_ = nullptr;

  // These need to be static so that we have their addresses available to
  // register as external references in the snapshot at environment creation
  // time.
  static v8::CFunction fast_number_;
  static v8::CFunction fast_bigint_;
};

}  // namespace process
}  // namespace node
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_NODE_PROCESS_H_

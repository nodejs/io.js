#ifndef SRC_NODE_REALM_H_
#define SRC_NODE_REALM_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <v8.h>
#include <unordered_map>
#include "cleanup_queue.h"
#include "cppgc_helpers.h"
#include "env_properties.h"
#include "memory_tracker.h"
#include "node_snapshotable.h"

namespace node {

struct RealmSerializeInfo {
  std::vector<std::string> builtins;
  std::vector<PropInfo> persistent_values;
  std::vector<PropInfo> native_objects;

  SnapshotIndex context;
  friend std::ostream& operator<<(std::ostream& o, const RealmSerializeInfo& i);
};

using BindingDataStore =
    std::array<BaseObjectWeakPtr<BaseObject>,
               static_cast<size_t>(BindingDataType::kBindingDataTypeCount)>;

class CppgcWrapperList
    : public ListHead<CppgcMixin, &CppgcMixin::wrapper_list_node_>,
      public MemoryRetainer {
 public:
  void Cleanup();

  SET_MEMORY_INFO_NAME(CppgcWrapperList)
  SET_SELF_SIZE(CppgcWrapperList)
  void MemoryInfo(MemoryTracker* tracker) const override;
};

/**
 * node::Realm is a container for a set of JavaScript objects and functions
 * that associated with a particular global environment.
 *
 * An ECMAScript realm (https://tc39.es/ecma262/#sec-code-realms) representing
 * a global environment in which script is run. Each ECMAScript realm comes
 * with a global object and a set of intrinsic objects. An ECMAScript realm has
 * a [[HostDefined]] field, which contains the node::Realm object.
 *
 * Realm can be a principal realm or a synthetic realm. A principal realm is
 * created with an Environment as its principal global environment to evaluate
 * scripts. A synthetic realm is created with JS APIs like ShadowRealm.
 *
 * Native bindings and builtin modules can be evaluated in either a principal
 * realm or a synthetic realm.
 */
class Realm : public MemoryRetainer {
 public:
  enum Kind {
    kPrincipal,
    kShadowRealm,
  };

  static inline Realm* GetCurrent(v8::Isolate* isolate);
  static inline Realm* GetCurrent(v8::Local<v8::Context> context);
  static inline Realm* GetCurrent(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  template <typename T>
  static inline Realm* GetCurrent(const v8::PropertyCallbackInfo<T>& info);

  Realm(Environment* env, v8::Local<v8::Context> context, Kind kind);

  Realm(const Realm&) = delete;
  Realm& operator=(const Realm&) = delete;
  Realm(Realm&&) = delete;
  Realm& operator=(Realm&&) = delete;

  void MemoryInfo(MemoryTracker* tracker) const override;

  void CreateProperties();
  RealmSerializeInfo Serialize(v8::SnapshotCreator* creator);
  void DeserializeProperties(const RealmSerializeInfo* info);

  v8::MaybeLocal<v8::Value> ExecuteBootstrapper(const char* id);
  v8::MaybeLocal<v8::Value> RunBootstrapping();

  inline void TrackBaseObject(BaseObject* bo);
  inline void UntrackBaseObject(BaseObject* bo);
  inline bool PendingCleanup() const;
  void RunCleanup();

  template <typename T>
  void ForEachBaseObject(T&& iterator) const;

  void PrintInfoForSnapshot();
  void VerifyNoStrongBaseObjects();

  inline IsolateData* isolate_data() const;
  inline Environment* env() const;
  inline v8::Isolate* isolate() const;
  inline Kind kind() const;
  virtual v8::Local<v8::Context> context() const;
  inline bool has_run_bootstrapping_code() const;

  // Methods created using SetMethod(), SetPrototypeMethod(), etc. inside
  // this scope can access the created T* object using
  // GetBindingData<T>(args) later.
  template <typename T, typename... Args>
  T* AddBindingData(v8::Local<v8::Object> target, Args&&... args);
  template <typename T, typename U>
  static inline T* GetBindingData(const v8::PropertyCallbackInfo<U>& info);
  template <typename T>
  static inline T* GetBindingData(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  template <typename T>
  static inline T* GetBindingData(v8::Local<v8::Context> context);
  template <typename T>
  inline T* GetBindingData();
  inline BindingDataStore* binding_data_store();

  // The BaseObject count is a debugging helper that makes sure that there are
  // no memory leaks caused by BaseObjects staying alive longer than expected
  // (in particular, no circular BaseObjectPtr references).
  inline int64_t base_object_count() const;

  // Base object count created after the bootstrap of the realm.
  inline int64_t base_object_created_after_bootstrap() const;

  inline void TrackCppgcWrapper(CppgcMixin* handle);
  inline CppgcWrapperList* cppgc_wrapper_list() { return &cppgc_wrapper_list_; }

#define V(PropertyName, TypeName)                                              \
  virtual v8::Local<TypeName> PropertyName() const = 0;                        \
  virtual void set_##PropertyName(v8::Local<TypeName> value) = 0;
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

  std::set<struct node_module*> internal_bindings;
  std::set<std::string> builtins_with_cache;
  std::set<std::string> builtins_without_cache;
  // This is only filled during deserialization. We use a vector since
  // it's only used for tests.
  std::vector<std::string> builtins_in_snapshot;

 protected:
  ~Realm();

  virtual v8::MaybeLocal<v8::Value> BootstrapRealm() = 0;

  Environment* env_;
  // Shorthand for isolate pointer.
  v8::Isolate* isolate_;
  v8::Global<v8::Context> context_;

#define V(PropertyName, TypeName) v8::Global<TypeName> PropertyName##_;
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

 private:
  void InitializeContext(v8::Local<v8::Context> context,
                         const RealmSerializeInfo* realm_info);
  void DoneBootstrapping();

  Kind kind_;
  bool has_run_bootstrapping_code_ = false;

  int64_t base_object_count_ = 0;
  int64_t base_object_created_by_bootstrap_ = 0;

  BindingDataStore binding_data_store_;

  BaseObjectList base_object_list_;
  CppgcWrapperList cppgc_wrapper_list_;
};

class PrincipalRealm : public Realm {
 public:
  PrincipalRealm(Environment* env,
                 v8::Local<v8::Context> context,
                 const RealmSerializeInfo* realm_info);
  ~PrincipalRealm();

  SET_MEMORY_INFO_NAME(PrincipalRealm)
  SET_SELF_SIZE(PrincipalRealm)

#define V(PropertyName, TypeName)                                              \
  v8::Local<TypeName> PropertyName() const override;                           \
  void set_##PropertyName(v8::Local<TypeName> value) override;
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

 protected:
  v8::MaybeLocal<v8::Value> BootstrapRealm() override;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_REALM_H_

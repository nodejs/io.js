#ifndef SRC_CPPGC_HELPERS_H_
#define SRC_CPPGC_HELPERS_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <type_traits>  // std::remove_reference
#include "cppgc/garbage-collected.h"
#include "cppgc/name-provider.h"
#include "memory_tracker.h"
#include "util.h"
#include "v8-cppgc.h"
#include "v8-sandbox.h"
#include "v8.h"

namespace node {

class Environment;
class Realm;
class CppgcWrapperList;

/**
 * This is a helper mixin with a BaseObject-like interface to help
 * implementing wrapper objects managed by V8's cppgc (Oilpan) library.
 * cppgc-manged objects in Node.js internals should extend this mixin,
 * while non-cppgc-managed objects typically extend BaseObject - the
 * latter are being migrated to be cppgc-managed wherever it's beneficial
 * and practical. Typically cppgc-managed objects are more efficient to
 * keep track of (which lowers initialization cost) and work better
 * with V8's GC scheduling.
 *
 * A cppgc-managed native wrapper should look something like this, note
 * that per cppgc rules, CPPGC_MIXIN(MyWrap) must be at the left-most
 * position in the hierarchy (which ensures cppgc::GarbageCollected
 * is at the left-most position).
 *
 * class MyWrap final : CPPGC_MIXIN(MyWrap) {
 *  public:
 *   SET_CPPGC_NAME(MyWrap)  // Sets the heap snapshot name to "Node / MyWrap"
 *   void Trace(cppgc::Visitor* visitor) const final {
 *     CppgcMixin::Trace(visitor);
 *     visitor->Trace(...);  // Trace any additional owned traceable data
 *   }
 * }
 *
 * If the wrapper needs to perform cleanups when it's destroyed and that
 * cleanup relies on a living Node.js `Environment`, it should implement a
 * pattern like this:
 *
 *   ~MyWrap() { this->Clean(); }
 *   void CleanEnvResource(Environment* env) override {
 *     // Do cleanup that relies on a living Environemnt.
 *   }
 */
class CppgcMixin : public cppgc::GarbageCollectedMixin, public MemoryRetainer {
 public:
  // To help various callbacks access wrapper objects with different memory
  // management, cppgc-managed objects share the same layout as BaseObjects.
  enum InternalFields { kEmbedderType = 0, kSlot, kInternalFieldCount };

  // The initialization cannot be done in the mixin constructor but has to be
  // invoked from the child class constructor, per cppgc::GarbageCollectedMixin
  // rules.
  template <typename T>
  static inline void Wrap(T* ptr, Realm* realm, v8::Local<v8::Object> obj);
  template <typename T>
  static inline void Wrap(T* ptr, Environment* env, v8::Local<v8::Object> obj);

  inline v8::Local<v8::Object> object() const;
  inline Environment* env() const;
  inline v8::Local<v8::Object> WrappedObject() const override;

  template <typename T>
  static inline T* Unwrap(v8::Local<v8::Object> obj);

  // Subclasses are expected to invoke CppgcMixin::Trace() in their own Trace()
  // methods.
  void Trace(cppgc::Visitor* visitor) const override {
    visitor->Trace(traced_reference_);
  }

  // TODO(joyeecheung): use ObjectSizeTrait;
  inline size_t SelfSize() const override { return sizeof(*this); }

  // This is run for all the remaining Cppgc wrappers tracked in the Realm
  // during Realm shutdown. The destruction of the wrappers would happen later,
  // when the final garbage collection is triggered when CppHeap is torn down as
  // part of the Isolate teardown. If subclasses of CppgcMixin wish to perform
  // cleanups that depend on the Realm during destruction, they should implment
  // it in a CleanEnvResource() override, and then call this->Clean() from their
  // destructor. Outside of CleanEnvResource(), subclasses should avoid calling
  // into JavaScript or perform any operation that can trigger garbage
  // collection during the destruction.
  void Clean() {
    if (realm_ == nullptr) return;
    this->CleanEnvResource(realm_);
    realm_ = nullptr;
  }

  // The default implementation of CleanEnvResource() is a no-op. Subclasses
  // should override it to perform cleanup that require a living Realm,
  // instead of doing these cleanups directly in the destructor.
  virtual void CleanEnvResource(Realm* realm) {}

  friend class CppgcWrapperList;

 private:
  Realm* realm_ = nullptr;
  v8::TracedReference<v8::Object> traced_reference_;
  ListNode<CppgcMixin> wrapper_list_node_;
};

// If the class doesn't have additional owned traceable data, use this macro to
// save the implementation of a custom Trace() method.
#define DEFAULT_CPPGC_TRACE()                                                  \
  void Trace(cppgc::Visitor* visitor) const final {                            \
    CppgcMixin::Trace(visitor);                                                \
  }

// This macro sets the node name in the heap snapshot with a "Node /" prefix.
// Classes that use this macro must extend cppgc::NameProvider.
#define SET_CPPGC_NAME(Klass)                                                  \
  inline const char* GetHumanReadableName() const final {                      \
    return "Node / " #Klass;                                                   \
  }                                                                            \
  inline const char* MemoryInfoName() const override {                         \
    return #Klass;                                                             \
  }

/**
 * Similar to ASSIGN_OR_RETURN_UNWRAP() but works on cppgc-managed types
 * inheriting CppgcMixin.
 */
#define ASSIGN_OR_RETURN_UNWRAP_CPPGC(ptr, obj, ...)                           \
  do {                                                                         \
    *ptr = CppgcMixin::Unwrap<                                                 \
        typename std::remove_reference<decltype(**ptr)>::type>(obj);           \
    if (*ptr == nullptr) return __VA_ARGS__;                                   \
  } while (0)
}  // namespace node

/**
 * Helper macro the manage the cppgc-based wrapper hierarchy. This must
 * be used at the left-most position - right after `:` in the class inheritance,
 * like this:
 * class Klass : CPPGC_MIXIN(Klass) ... {}
 *
 * This needs to disable linters because it will be at odds with
 * clang-format.
 */
#define CPPGC_MIXIN(Klass)                                                     \
  public /* NOLINT(whitespace/indent) */                                       \
  cppgc::GarbageCollected<Klass>, public cppgc::NameProvider, public CppgcMixin

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_CPPGC_HELPERS_H_

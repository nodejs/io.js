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

#ifndef SRC_BASE_OBJECT_H_
#define SRC_BASE_OBJECT_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <type_traits>  // std::remove_reference
#include "base_object_types.h"
#include "memory_tracker.h"
#include "util.h"
#include "v8.h"

namespace node {

class Environment;
class IsolateData;
class Realm;
template <typename T, bool kIsWeak>
class BaseObjectPtrImpl;

namespace worker {
class TransferData;
}

class BaseObject : public MemoryRetainer {
 public:
  enum InternalFields { kEmbedderType, kSlot, kInternalFieldCount };

  // Associates this object with `object`. It uses the 1st internal field for
  // that, and in particular aborts if there is no such field.
  // This is the designated constructor.
  BaseObject(Realm* realm, v8::Local<v8::Object> object);
  // Convenient constructor for constructing BaseObject in the principal realm.
  inline BaseObject(Environment* env, v8::Local<v8::Object> object);
  ~BaseObject() override;

  BaseObject() = delete;

  // Returns the wrapped object.  Returns an empty handle when
  // persistent.IsEmpty() is true.
  inline v8::Local<v8::Object> object() const;

  // Same as the above, except it additionally verifies that this object
  // is associated with the passed Isolate in debug mode.
  inline v8::Local<v8::Object> object(v8::Isolate* isolate) const;

  inline v8::Global<v8::Object>& persistent();

  inline Environment* env() const;
  inline Realm* realm() const;

  // Get a BaseObject* pointer, or subclass pointer, for the JS object that
  // was also passed to the `BaseObject()` constructor initially.
  // This may return `nullptr` if the C++ object has not been constructed yet,
  // e.g. when the JS object used `MakeLazilyInitializedJSTemplate`.
  static inline void SetInternalFields(IsolateData* isolate_data,
                                       v8::Local<v8::Object> object,
                                       void* slot);
  static inline bool IsBaseObject(IsolateData* isolate_data,
                                  v8::Local<v8::Object> object);
  static inline void TagBaseObject(IsolateData* isolate_data,
                                   v8::Local<v8::Object> object);
  static void LazilyInitializedJSTemplateConstructor(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static inline BaseObject* FromJSObject(v8::Local<v8::Value> object);
  template <typename T>
  static inline T* FromJSObject(v8::Local<v8::Value> object);
  // Global alias for FromJSObject() to avoid churn.
  template <typename T>
  static inline T* Unwrap(v8::Local<v8::Value> obj) {
    return BaseObject::FromJSObject<T>(obj);
  }

  // Make the `v8::Global` a weak reference and, `delete` this object once
  // the JS object has been garbage collected and there are no (strong)
  // BaseObjectPtr references to it.
  void MakeWeak();

  // Undo `MakeWeak()`, i.e. turn this into a strong reference that is a GC
  // root and will not be touched by the garbage collector.
  inline void ClearWeak();

  // Reports whether this BaseObject is using a weak reference or detached,
  // i.e. whether is can be deleted by GC once no strong BaseObjectPtrs refer
  // to it anymore.
  inline bool IsWeakOrDetached() const;

  inline v8::EmbedderGraph::Node::Detachedness GetDetachedness() const override;

  // Utility to create a FunctionTemplate with one internal field (used for
  // the `BaseObject*` pointer) and a constructor that initializes that field
  // to `nullptr`.
  static v8::Local<v8::FunctionTemplate> MakeLazilyInitializedJSTemplate(
      IsolateData* isolate);
  static v8::Local<v8::FunctionTemplate> MakeLazilyInitializedJSTemplate(
      Environment* env);

  // Setter/Getter pair for internal fields that can be passed to SetAccessor.
  template <int Field>
  static void InternalFieldGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  template <int Field, bool (v8::Value::*typecheck)() const>
  static void InternalFieldSet(const v8::FunctionCallbackInfo<v8::Value>& args);

  // This is a bit of a hack. See the override in async_wrap.cc for details.
  virtual bool IsDoneInitializing() const;

  // Can be used to avoid this object keeping itself alive as a GC root
  // indefinitely, for example when this object is owned and deleted by another
  // BaseObject once that is torn down. This can only be called when there is
  // a BaseObjectPtr to this object.
  inline void Detach();

  static inline v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      IsolateData* isolate_data);

  // Interface for transferring BaseObject instances using the .postMessage()
  // method of MessagePorts (and, by extension, Workers).
  // GetTransferMode() returns a transfer mode that indicates how to deal with
  // the current object:
  // - kDisallowCloneAndTransfer:
  //     No transfer or clone is possible, either because this type of
  //     BaseObject does not know how to be transferred, or because it is not
  //     in a state in which it is possible to do so (e.g. because it has
  //     already been transferred).
  // - kTransferable:
  //     This object can be transferred in a destructive fashion, i.e. will be
  //     rendered unusable on the sending side of the channel in the process
  //     of being transferred. (In C++ this would be referred to as movable but
  //     not copyable.) Objects of this type need to be listed in the
  //     `transferList` argument of the relevant postMessage() call in order to
  //     make sure that they are not accidentally destroyed on the sending side.
  //     TransferForMessaging() will be called to get a representation of the
  //     object that is used for subsequent deserialization.
  //     The NestedTransferables() method can be used to transfer other objects
  //     along with this one, if a situation requires it.
  // - kCloneable:
  //     This object can be cloned without being modified.
  //     CloneForMessaging() will be called to get a representation of the
  //     object that is used for subsequent deserialization, unless the
  //     object is listed in transferList and is kTransferable, in which case
  //     TransferForMessaging() is attempted first.
  // - kTransferableAndCloneable:
  //     This object can be transferred or cloned.
  // After a successful clone, FinalizeTransferRead() is called on the receiving
  // end, and can read deserialize JS data possibly serialized by a previous
  // FinalizeTransferWrite() call.
  // By default, a BaseObject is kDisallowCloneAndTransfer and a JS Object is
  // kCloneable unless otherwise specified.
  enum TransferMode : uint32_t {
    kDisallowCloneAndTransfer = 0,
    kTransferable = 1 << 0,
    kCloneable = 1 << 1,
    kTransferableAndCloneable = kTransferable | kCloneable,
  };
  virtual TransferMode GetTransferMode() const;
  virtual std::unique_ptr<worker::TransferData> TransferForMessaging();
  virtual std::unique_ptr<worker::TransferData> CloneForMessaging() const;
  virtual v8::Maybe<std::vector<BaseObjectPtrImpl<BaseObject, false>>>
      NestedTransferables() const;
  virtual v8::Maybe<void> FinalizeTransferRead(
      v8::Local<v8::Context> context, v8::ValueDeserializer* deserializer);

  // Indicates whether this object is expected to use a strong reference during
  // a clean process exit (due to an empty event loop).
  virtual bool IsNotIndicativeOfMemoryLeakAtExit() const;

  virtual inline void OnGCCollect();

  virtual inline bool is_snapshotable() const { return false; }

 private:
  v8::Local<v8::Object> WrappedObject() const override;
  bool IsRootNode() const override;
  void DeleteMe();

  // persistent_handle_ needs to be at a fixed offset from the start of the
  // class because it is used by src/node_postmortem_metadata.cc to calculate
  // offsets and generate debug symbols for BaseObject, which assumes that the
  // position of members in memory are predictable. For more information please
  // refer to `doc/contributing/node-postmortem-support.md`
  friend int GenDebugSymbols();
  friend class CleanupQueue;
  template <typename T, bool kIsWeak>
  friend class BaseObjectPtrImpl;

  v8::Global<v8::Object> persistent_handle_;

  // Metadata that is associated with this BaseObject if there are BaseObjectPtr
  // or BaseObjectWeakPtr references to it.
  // This object is deleted when the BaseObject itself is destroyed, and there
  // are no weak references to it.
  struct PointerData {
    // Number of BaseObjectPtr instances that refer to this object. If this
    // is non-zero, the BaseObject is always a GC root and will not be destroyed
    // during cleanup until the count drops to zero again.
    unsigned int strong_ptr_count = 0;
    // Number of BaseObjectWeakPtr instances that refer to this object.
    unsigned int weak_ptr_count = 0;
    // Indicates whether MakeWeak() has been called.
    bool wants_weak_jsobj = false;
    // Indicates whether Detach() has been called. If that is the case, this
    // object will be destroyed once the strong pointer count drops to zero.
    bool is_detached = false;
    // Reference to the original BaseObject. This is used by weak pointers.
    BaseObject* self = nullptr;
  };

  inline bool has_pointer_data() const;
  // This creates a PointerData struct if none was associated with this
  // BaseObject before.
  PointerData* pointer_data();

  // Functions that adjust the strong pointer count.
  void decrease_refcount();
  void increase_refcount();

  Realm* realm_;
  PointerData* pointer_data_ = nullptr;
  ListNode<BaseObject> base_object_list_node_;

  friend class BaseObjectList;
};

class BaseObjectList
    : public ListHead<BaseObject, &BaseObject::base_object_list_node_>,
      public MemoryRetainer {
 public:
  void Cleanup();

  SET_MEMORY_INFO_NAME(BaseObjectList)
  SET_SELF_SIZE(BaseObjectList)
  void MemoryInfo(node::MemoryTracker* tracker) const override;
};

#define ASSIGN_OR_RETURN_UNWRAP(ptr, obj, ...)                                 \
  do {                                                                         \
    *ptr = static_cast<typename std::remove_reference<decltype(*ptr)>::type>(  \
        BaseObject::FromJSObject(obj));                                        \
    if (*ptr == nullptr) return __VA_ARGS__;                                   \
  } while (0)

// Implementation of a generic strong or weak pointer to a BaseObject.
// If strong, this will keep the target BaseObject alive regardless of other
// circumstances such as the GC or Environment cleanup.
// If weak, destruction behaviour is not affected, but the pointer will be
// reset to nullptr once the BaseObject is destroyed.
// The API matches std::shared_ptr closely. However, this class is not thread
// safe, that is, we can't have different BaseObjectPtrImpl instances in
// different threads referring to the same BaseObject instance.
template <typename T, bool kIsWeak>
class BaseObjectPtrImpl final {
 public:
  inline BaseObjectPtrImpl();
  inline ~BaseObjectPtrImpl();
  inline explicit BaseObjectPtrImpl(T* target);

  // Copy and move constructors. Note that the templated version is not a copy
  // or move constructor in the C++ sense of the word, so an identical
  // untemplated version is provided.
  template <typename U, bool kW>
  inline BaseObjectPtrImpl(const BaseObjectPtrImpl<U, kW>& other);
  inline BaseObjectPtrImpl(const BaseObjectPtrImpl& other);
  template <typename U, bool kW>
  inline BaseObjectPtrImpl& operator=(const BaseObjectPtrImpl<U, kW>& other);
  inline BaseObjectPtrImpl& operator=(const BaseObjectPtrImpl& other);
  inline BaseObjectPtrImpl(BaseObjectPtrImpl&& other);
  inline BaseObjectPtrImpl& operator=(BaseObjectPtrImpl&& other);

  inline void reset(T* ptr = nullptr);
  inline T* get() const;
  inline T& operator*() const;
  inline T* operator->() const;
  inline operator bool() const;

  template <typename U, bool kW>
  inline bool operator ==(const BaseObjectPtrImpl<U, kW>& other) const;
  template <typename U, bool kW>
  inline bool operator !=(const BaseObjectPtrImpl<U, kW>& other) const;

 private:
  union {
    BaseObject* target;                     // Used for strong pointers.
    BaseObject::PointerData* pointer_data;  // Used for weak pointers.
  } data_;

  inline BaseObject* get_base_object() const;
  inline BaseObject::PointerData* pointer_data() const;
};

template <typename T>
using BaseObjectPtr = BaseObjectPtrImpl<T, false>;
template <typename T>
using BaseObjectWeakPtr = BaseObjectPtrImpl<T, true>;

// Create a BaseObject instance and return a pointer to it.
// This variant leaves the object as a GC root by default.
template <typename T, typename... Args>
inline BaseObjectPtr<T> MakeBaseObject(Args&&... args);
// Create a BaseObject instance and return a pointer to it.
// This variant makes the object a weak GC root by default.
template <typename T, typename... Args>
inline BaseObjectWeakPtr<T> MakeWeakBaseObject(Args&&... args);
// Create a BaseObject instance and return a pointer to it.
// This variant detaches the object by default, meaning that the caller fully
// owns it, and once the last BaseObjectPtr to it is destroyed, the object
// itself is also destroyed.
template <typename T, typename... Args>
inline BaseObjectPtr<T> MakeDetachedBaseObject(Args&&... args);

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_BASE_OBJECT_H_

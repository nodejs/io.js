
#ifndef SRC_NODE_SNAPSHOTABLE_H_
#define SRC_NODE_SNAPSHOTABLE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "aliased_buffer.h"
#include "base_object.h"
#include "util.h"

namespace node {

class Environment;
struct RealmSerializeInfo;
struct SnapshotData;
class ExternalReferenceRegistry;

using SnapshotIndex = size_t;

struct PropInfo {
  std::string name;     // name for debugging
  uint32_t id;          // In the list - in case there are any empty entries
  SnapshotIndex index;  // In the snapshot
};

typedef size_t SnapshotIndex;

bool WithoutCodeCache(const SnapshotFlags& flags);
bool WithoutCodeCache(const SnapshotConfig& config);

// When serializing an embedder object, we'll serialize the native states
// into a chunk that can be mapped into a subclass of InternalFieldInfoBase,
// and pass it into the V8 callback as the payload of StartupData.
// The memory chunk looks like this:
//
// [   type   ] - EmbedderObjectType (a uint8_t)
// [  length  ] - a size_t
// [    ...   ] - custom bytes of size |length - header size|
struct InternalFieldInfoBase {
 public:
  EmbedderObjectType type;
  size_t length;

  template <typename T>
  static T* New(EmbedderObjectType type) {
    static_assert(std::is_base_of_v<InternalFieldInfoBase, T> ||
                      std::is_same_v<InternalFieldInfoBase, T>,
                  "Can only accept InternalFieldInfoBase subclasses");
    void* buf = ::operator new[](sizeof(T));
    memset(buf, 0, sizeof(T));  // Make the padding reproducible.
    T* result = new (buf) T;
    result->type = type;
    result->length = sizeof(T);
    return result;
  }

  template <typename T>
  T* Copy() const {
    static_assert(std::is_base_of_v<InternalFieldInfoBase, T> ||
                      std::is_same_v<InternalFieldInfoBase, T>,
                  "Can only accept InternalFieldInfoBase subclasses");
    static_assert(std::is_trivially_copyable_v<T>,
                  "Can only memcpy trivially copyable class");
    void* buf = ::operator new[](sizeof(T));
    T* result = new (buf) T;
    memcpy(result, this, sizeof(T));
    return result;
  }

  void Delete() { ::operator delete[](this); }

  InternalFieldInfoBase() = default;
};

struct EmbedderTypeInfo {
  enum class MemoryMode : uint8_t { kBaseObject, kCppGC };
  EmbedderTypeInfo(EmbedderObjectType t, MemoryMode m) : type(t), mode(m) {}
  EmbedderTypeInfo() = default;
  EmbedderObjectType type;
  MemoryMode mode;
};

// An interface for snapshotable native objects to inherit from.
// Use the SERIALIZABLE_OBJECT_METHODS() macro in the class to define
// the following methods to implement:
//
// - PrepareForSerialization(): This would be run prior to context
//   serialization. Use this method to e.g. release references that
//   can be re-initialized, or perform property store operations
//   that needs a V8 context.
// - Serialize(): This would be called during context serialization,
//   once for each embedder field of the object.
//   Allocate and construct an InternalFieldInfoBase object that contains
//   data that can be used to deserialize native states.
// - Deserialize(): This would be called after the context is
//   deserialized and the object graph is complete, once for each
//   embedder field of the object. Use this to restore native states
//   in the object.
class SnapshotableObject : public BaseObject {
 public:
  SnapshotableObject(Realm* realm,
                     v8::Local<v8::Object> wrap,
                     EmbedderObjectType type);
  std::string GetTypeName() const;

  // If returns false, the object will not be serialized.
  virtual bool PrepareForSerialization(v8::Local<v8::Context> context,
                                       v8::SnapshotCreator* creator) = 0;
  virtual InternalFieldInfoBase* Serialize(int index) = 0;
  bool is_snapshotable() const override { return true; }
  // We'll make sure that the type is set in the constructor
  EmbedderObjectType type() { return type_; }

 private:
  EmbedderObjectType type_;
};

#define SERIALIZABLE_OBJECT_METHODS()                                          \
  bool PrepareForSerialization(v8::Local<v8::Context> context,                 \
                               v8::SnapshotCreator* creator) override;         \
  InternalFieldInfoBase* Serialize(int index) override;                        \
  static void Deserialize(v8::Local<v8::Context> context,                      \
                          v8::Local<v8::Object> holder,                        \
                          int index,                                           \
                          InternalFieldInfoBase* info);

v8::StartupData SerializeNodeContextInternalFields(v8::Local<v8::Object> holder,
                                                   int index,
                                                   void* env);
v8::StartupData SerializeNodeContextData(v8::Local<v8::Context> holder,
                                         int index,
                                         void* env);
void DeserializeNodeInternalFields(v8::Local<v8::Object> holder,
                                   int index,
                                   v8::StartupData payload,
                                   void* env);
void DeserializeNodeContextData(v8::Local<v8::Context> holder,
                                int index,
                                v8::StartupData payload,
                                void* env);
void SerializeSnapshotableObjects(Realm* realm,
                                  v8::SnapshotCreator* creator,
                                  RealmSerializeInfo* info);

#define DCHECK_IS_SNAPSHOT_SLOT(index) DCHECK_EQ(index, BaseObject::kSlot)

namespace mksnapshot {
class BindingData : public SnapshotableObject {
 public:
  struct InternalFieldInfo : public node::InternalFieldInfoBase {
    AliasedBufferIndex is_building_snapshot_buffer;
  };

  BindingData(Realm* realm,
              v8::Local<v8::Object> obj,
              InternalFieldInfo* info = nullptr);
  SET_BINDING_ID(mksnapshot_binding_data)
  SERIALIZABLE_OBJECT_METHODS()

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_SELF_SIZE(BindingData)
  SET_MEMORY_INFO_NAME(BindingData)

 private:
  AliasedUint8Array is_building_snapshot_buffer_;
  InternalFieldInfo* internal_field_info_ = nullptr;
};
}  // namespace mksnapshot

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_SNAPSHOTABLE_H_

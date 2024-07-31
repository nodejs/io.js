#ifndef SRC_BASE_OBJECT_TYPES_H_
#define SRC_BASE_OBJECT_TYPES_H_

#include <cinttypes>

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

namespace node {
// List of internalBinding() data wrappers. The first argument should match
// what the class passes to SET_BINDING_ID(), the second argument should match
// the C++ class name.
#define SERIALIZABLE_BINDING_TYPES(V)                                          \
  V(encoding_binding_data, encoding_binding::BindingData)                      \
  V(fs_binding_data, fs::BindingData)                                          \
  V(mksnapshot_binding_data, mksnapshot::BindingData)                          \
  V(v8_binding_data, v8_utils::BindingData)                                    \
  V(blob_binding_data, BlobBindingData)                                        \
  V(process_binding_data, process::BindingData)                                \
  V(timers_binding_data, timers::BindingData)                                  \
  V(url_binding_data, url::BindingData)                                        \
  V(modules_binding_data, modules::BindingData)

#define UNSERIALIZABLE_BINDING_TYPES(V)                                        \
  V(http2_binding_data, http2::BindingData)                                    \
  V(http_parser_binding_data, http_parser::BindingData)                        \
  V(quic_binding_data, quic::BindingData)

// List of (non-binding) BaseObjects that are serializable in the snapshot.
// The first argument should match what the type passes to
// SET_OBJECT_ID(), the second argument should match the C++ class
// name.
#define SERIALIZABLE_NON_BINDING_TYPES(V)

// Helper list of all binding data wrapper types.
#define BINDING_TYPES(V)                                                       \
  SERIALIZABLE_BINDING_TYPES(V)                                                \
  UNSERIALIZABLE_BINDING_TYPES(V)

// Helper list of all BaseObjects that implement snapshot support.
#define SERIALIZABLE_OBJECT_TYPES(V)                                           \
  SERIALIZABLE_BINDING_TYPES(V)                                                \
  SERIALIZABLE_NON_BINDING_TYPES(V)

#define V(TypeId, NativeType) k_##TypeId,
enum class BindingDataType : uint8_t { BINDING_TYPES(V) kBindingDataTypeCount };
// Make sure that we put the bindings first so that we can also use the enums
// for the bindings as index to the binding data store.
enum class EmbedderObjectType : uint8_t {
  BINDING_TYPES(V) SERIALIZABLE_NON_BINDING_TYPES(V)
  // We do not need to know about all the unserializable non-binding types for
  // now so we do not list them.
  kEmbedderObjectTypeCount
};
#undef V

// For now, BaseObjects only need to call this when they implement snapshot
// support.
#define SET_OBJECT_ID(TypeId)                                                  \
  static constexpr EmbedderObjectType type_int = EmbedderObjectType::k_##TypeId;

// Binding data should call this so that they can be looked up from the binding
// data store.
#define SET_BINDING_ID(TypeId)                                                 \
  static constexpr BindingDataType binding_type_int =                          \
      BindingDataType::k_##TypeId;                                             \
  SET_OBJECT_ID(TypeId)                                                        \
  static_assert(static_cast<uint8_t>(type_int) ==                              \
                static_cast<uint8_t>(binding_type_int));

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_BASE_OBJECT_TYPES_H_

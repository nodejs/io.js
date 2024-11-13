// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the public interface to v8_debug_helper.

#ifndef V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_H_
#define V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_H_

#include <cstdint>
#include <memory>

#if defined(_WIN32)

#ifdef BUILDING_V8_DEBUG_HELPER
#define V8_DEBUG_HELPER_EXPORT __declspec(dllexport)
#elif USING_V8_DEBUG_HELPER
#define V8_DEBUG_HELPER_EXPORT __declspec(dllimport)
#else
#define V8_DEBUG_HELPER_EXPORT
#endif

#else  // defined(_WIN32)

#ifdef BUILDING_V8_DEBUG_HELPER
#define V8_DEBUG_HELPER_EXPORT __attribute__((visibility("default")))
#else
#define V8_DEBUG_HELPER_EXPORT
#endif

#endif  // defined(_WIN32)

namespace v8 {
namespace debug_helper {

// Possible results when attempting to fetch memory from the debuggee.
enum class MemoryAccessResult {
  kOk,
  kAddressNotValid,
  kAddressValidButInaccessible,  // Possible in incomplete dump.
};

// Information about how this tool discovered the type of the object.
enum class TypeCheckResult {
  // Success cases:
  kSmi,
  kWeakRef,
  kUsedMap,
  kKnownMapPointer,
  kUsedTypeHint,

  // Failure cases:
  kUnableToDecompress,  // Caller must provide the heap range somehow.
  kObjectPointerInvalid,
  kObjectPointerValidButInaccessible,  // Possible in incomplete dump.
  kMapPointerInvalid,
  kMapPointerValidButInaccessible,  // Possible in incomplete dump.
  kUnknownInstanceType,
  kUnknownTypeHint,
};

enum class PropertyKind {
  kSingle,
  kArrayOfKnownSize,
  kArrayOfUnknownSizeDueToInvalidMemory,
  kArrayOfUnknownSizeDueToValidButInaccessibleMemory,
};

struct PropertyBase {
  const char* name;

  // Statically-determined type, such as from .tq definition. Can be an empty
  // string if this property is itself a Torque-defined struct; in that case use
  // |struct_fields| instead. This type should be treated as if it were used in
  // the v8::internal namespace; that is, type "X::Y" can mean any of the
  // following, in order of decreasing preference:
  // - v8::internal::X::Y
  // - v8::X::Y
  // - X::Y
  const char* type;
};

struct StructProperty : public PropertyBase {
  // The offset from the beginning of the struct to this field.
  size_t offset;

  // The number of bits that are present, if this value is a bitfield. Zero
  // indicates that this value is not a bitfield (the full value is stored).
  uint8_t num_bits;

  // The number of bits by which this value has been left-shifted for storage as
  // a bitfield.
  uint8_t shift_bits;
};

struct ObjectProperty : public PropertyBase {
  // The address where the property value can be found in the debuggee's address
  // space, or the address of the first value for an array.
  uintptr_t address;

  // If kind indicates an array of unknown size, num_values will be 0 and debug
  // tools should display this property as a raw pointer. Note that there is a
  // semantic difference between num_values=1 and kind=kSingle (normal property)
  // versus num_values=1 and kind=kArrayOfKnownSize (one-element array).
  size_t num_values;

  // The number of bytes occupied by a single instance of the value type for
  // this property. This can also be used as the array stride because arrays are
  // tightly packed like in C.
  size_t size;

  // If the property is a struct made up of several pieces of data packed
  // together, then the |struct_fields| array contains descriptions of those
  // fields.
  size_t num_struct_fields;
  StructProperty** struct_fields;

  PropertyKind kind;
};

struct ObjectPropertiesResult {
  TypeCheckResult type_check_result;
  const char* brief;
  const char* type;  // Runtime type of the object.
  size_t num_properties;
  ObjectProperty** properties;

  // If not all relevant memory is available, GetObjectProperties may respond
  // with a technically correct but uninteresting type such as HeapObject, and
  // use other heuristics to make reasonable guesses about what specific type
  // the object actually is. You may request data about the same object again
  // using any of these guesses as the type hint, but the results should be
  // formatted to the user in a way that clearly indicates that they're only
  // guesses.
  size_t num_guessed_types;
  const char** guessed_types;
};

struct StackFrameResult {
  size_t num_properties;
  ObjectProperty** properties;
};

// Copies byte_count bytes of memory from the given address in the debuggee to
// the destination buffer.
typedef MemoryAccessResult (*MemoryAccessor)(uintptr_t address,
                                             void* destination,
                                             size_t byte_count);

// Additional data that can help GetObjectProperties to be more accurate. Any
// fields you don't know can be set to zero and this library will do the best it
// can with the information available.
struct HeapAddresses {
  // Beginning of allocated space for various kinds of data. These can help us
  // to detect certain common objects that are placed in memory during startup.
  // These values might be provided via name-value pairs in CrashPad dumps.
  // Otherwise, they can be obtained as follows:
  // 1. Get the Isolate pointer for the current thread. It might be somewhere on
  //    the stack, or it might be accessible from thread-local storage with the
  //    key stored in v8::internal::Isolate::isolate_key_.
  // 2. Get isolate->heap_.map_space_->memory_chunk_list_.front_ and similar for
  //    old_space_ and read_only_space_.
  uintptr_t map_space_first_page;
  uintptr_t old_space_first_page;
  uintptr_t read_only_space_first_page;

  // Any valid heap pointer address. On platforms where pointer compression is
  // enabled, this can allow us to get data from compressed pointers even if the
  // other data above is not provided.
  uintptr_t any_heap_pointer;

  // A pointer to the static array
  // v8::internal::MemoryChunk::metadata_pointer_table_.
  uintptr_t metadata_pointer_table;
};

}  // namespace debug_helper
}  // namespace v8

extern "C" {
// Raw library interface. If possible, use functions in v8::debug_helper
// namespace instead because they use smart pointers to prevent leaks.
V8_DEBUG_HELPER_EXPORT v8::debug_helper::ObjectPropertiesResult*
_v8_debug_helper_GetObjectProperties(
    uintptr_t object, v8::debug_helper::MemoryAccessor memory_accessor,
    const v8::debug_helper::HeapAddresses& heap_addresses,
    const char* type_hint);
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_Free_ObjectPropertiesResult(
    v8::debug_helper::ObjectPropertiesResult* result);
V8_DEBUG_HELPER_EXPORT v8::debug_helper::StackFrameResult*
_v8_debug_helper_GetStackFrame(
    uintptr_t frame_pointer, v8::debug_helper::MemoryAccessor memory_accessor);
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_Free_StackFrameResult(
    v8::debug_helper::StackFrameResult* result);
V8_DEBUG_HELPER_EXPORT const char* _v8_debug_helper_BitsetName(
    uint64_t payload);
}

namespace v8 {
namespace debug_helper {

struct DebugHelperObjectPropertiesResultDeleter {
  void operator()(v8::debug_helper::ObjectPropertiesResult* ptr) {
    _v8_debug_helper_Free_ObjectPropertiesResult(ptr);
  }
};
using ObjectPropertiesResultPtr =
    std::unique_ptr<ObjectPropertiesResult,
                    DebugHelperObjectPropertiesResultDeleter>;

// Get information about the given object pointer, which could be:
// - A tagged pointer, strong or weak
// - A cleared weak pointer
// - A compressed tagged pointer, zero-extended to 64 bits
// - A tagged small integer
// The type hint is only used if the object's Map is missing or corrupt. It
// should be the fully-qualified name of a class that inherits from
// v8::internal::Object.
inline ObjectPropertiesResultPtr GetObjectProperties(
    uintptr_t object, v8::debug_helper::MemoryAccessor memory_accessor,
    const HeapAddresses& heap_addresses, const char* type_hint = nullptr) {
  return ObjectPropertiesResultPtr(_v8_debug_helper_GetObjectProperties(
      object, memory_accessor, heap_addresses, type_hint));
}

// Return a bitset name for a v8::internal::compiler::Type with payload or null
// if the payload is not a bitset.
inline const char* BitsetName(uint64_t payload) {
  return _v8_debug_helper_BitsetName(payload);
}

struct DebugHelperStackFrameResultDeleter {
  void operator()(v8::debug_helper::StackFrameResult* ptr) {
    _v8_debug_helper_Free_StackFrameResult(ptr);
  }
};
using StackFrameResultPtr =
    std::unique_ptr<StackFrameResult, DebugHelperStackFrameResultDeleter>;

inline StackFrameResultPtr GetStackFrame(
    uintptr_t frame_pointer, v8::debug_helper::MemoryAccessor memory_accessor) {
  return StackFrameResultPtr(
      _v8_debug_helper_GetStackFrame(frame_pointer, memory_accessor));
}

}  // namespace debug_helper
}  // namespace v8

#endif

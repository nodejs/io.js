// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SERIALIZER_DESERIALIZER_H_
#define V8_SNAPSHOT_SERIALIZER_DESERIALIZER_H_

#include "src/objects/visitors.h"
#include "src/snapshot/references.h"

namespace v8 {
namespace internal {

class Isolate;

// The Serializer/Deserializer class is a common superclass for Serializer and
// Deserializer which is used to store common constants and methods used by
// both.
class SerializerDeserializer : public RootVisitor {
 public:
  static void IterateStartupObjectCache(Isolate* isolate, RootVisitor* visitor);

  static void IterateSharedHeapObjectCache(Isolate* isolate,
                                           RootVisitor* visitor);

 protected:
  enum class SlotType {
    kAnySlot,
    kMapSlot,
  };
  static bool CanBeDeferred(Tagged<HeapObject> o, SlotType slot_type);

  void RestoreExternalReferenceRedirector(Isolate* isolate,
                                          Tagged<AccessorInfo> accessor_info);
  void RestoreExternalReferenceRedirector(
      Isolate* isolate, Tagged<FunctionTemplateInfo> function_template_info);

  // clang-format off
#define UNUSED_SERIALIZER_BYTE_CODES(V)                           \
  /* Free range 0x22..0x2f */                                     \
                  V(0x22) V(0x23) V(0x24) V(0x25) V(0x26) V(0x27) \
  V(0x28) V(0x29) V(0x2a) V(0x2b) V(0x2c) V(0x2d) V(0x2e) V(0x2f) \
  /* Free range 0x30..0x3f */                                     \
  V(0x30) V(0x31) V(0x32) V(0x33) V(0x34) V(0x35) V(0x36) V(0x37) \
  V(0x38) V(0x39) V(0x3a) V(0x3b) V(0x3c) V(0x3d) V(0x3e) V(0x3f) \
  /* Free range 0x97..0x9f */                                     \
  V(0x98) V(0x99) V(0x9a) V(0x9b) V(0x9c) V(0x9d) V(0x9e) V(0x9f) \
  /* Free range 0xa0..0xaf */                                     \
  V(0xa0) V(0xa1) V(0xa2) V(0xa3) V(0xa4) V(0xa5) V(0xa6) V(0xa7) \
  V(0xa8) V(0xa9) V(0xaa) V(0xab) V(0xac) V(0xad) V(0xae) V(0xaf) \
  /* Free range 0xb0..0xbf */                                     \
  V(0xb0) V(0xb1) V(0xb2) V(0xb3) V(0xb4) V(0xb5) V(0xb6) V(0xb7) \
  V(0xb8) V(0xb9) V(0xba) V(0xbb) V(0xbc) V(0xbd) V(0xbe) V(0xbf) \
  /* Free range 0xc0..0xcf */                                     \
  V(0xc0) V(0xc1) V(0xc2) V(0xc3) V(0xc4) V(0xc5) V(0xc6) V(0xc7) \
  V(0xc8) V(0xc9) V(0xca) V(0xcb) V(0xcc) V(0xcd) V(0xce) V(0xcf) \
  /* Free range 0xd0..0xdf */                                     \
  V(0xd0) V(0xd1) V(0xd2) V(0xd3) V(0xd4) V(0xd5) V(0xd6) V(0xd7) \
  V(0xd8) V(0xd9) V(0xda) V(0xdb) V(0xdc) V(0xdd) V(0xde) V(0xdf) \
  /* Free range 0xe0..0xef */                                     \
  V(0xe0) V(0xe1) V(0xe2) V(0xe3) V(0xe4) V(0xe5) V(0xe6) V(0xe7) \
  V(0xe8) V(0xe9) V(0xea) V(0xeb) V(0xec) V(0xed) V(0xee) V(0xef) \
  /* Free range 0xf0..0xff */                                     \
  V(0xf0) V(0xf1) V(0xf2) V(0xf3) V(0xf4) V(0xf5) V(0xf6) V(0xf7) \
  V(0xf8) V(0xf9) V(0xfa) V(0xfb) V(0xfc) V(0xfd) V(0xfe) V(0xff)
  // clang-format on

  // The static assert below will trigger when the number of preallocated spaces
  // changed. If that happens, update the kNewObject and kBackref bytecode
  // ranges in the comments below.
  static_assert(4 == kNumberOfSnapshotSpaces);

  // First 32 root array items.
  static const int kRootArrayConstantsCount = 0x20;

  // 32 common raw data lengths.
  static const int kFixedRawDataCount = 0x20;
  // 16 repeats lengths.
  static const int kFixedRepeatRootCount = 0x10;

  // 8 hot (recently seen or back-referenced) objects with optional skip.
  static const int kHotObjectCount = 8;

  enum Bytecode : uint8_t {
    //
    // ---------- byte code range 0x00..0x1f ----------
    //

    // 0x00..0x03  Allocate new object, in specified space.
    kNewObject = 0x00,
    // Reference to previously allocated object.
    kBackref = 0x04,
    // Reference to an object in the read only heap.
    kReadOnlyHeapRef,
    // Object in the startup object cache.
    kStartupObjectCache,
    // Root array item.
    kRootArray,
    // Object provided in the attached list.
    kAttachedReference,
    // Object in the shared heap object cache.
    kSharedHeapObjectCache,
    // Do nothing, used for padding.
    kNop,
    // A tag emitted at strategic points in the snapshot to delineate sections.
    // If the deserializer does not find these at the expected moments then it
    // is an indication that the snapshot and the VM do not fit together.
    // Examine the build process for architecture, version or configuration
    // mismatches.
    kSynchronize,
    // Repeats of variable length of a root.
    kVariableRepeatRoot,
    // Used for embedder-allocated backing stores for TypedArrays.
    kOffHeapBackingStore,
    kOffHeapResizableBackingStore,
    // Used for embedder-provided serialization data for embedder fields.
    kEmbedderFieldsData,
    // Used for embedder-provided serialziation data for API wrappers.
    kApiWrapperFieldsData,
    // Raw data of variable length.
    kVariableRawData,
    // Used to encode external references provided through the API.
    kApiReference,
    // External reference referenced by id.
    kExternalReference,
    // Same as three bytecodes above but for serializing sandboxed external
    // pointer values.
    // TODO(v8:10391): Remove them once all ExternalPointer usages are
    // sandbox-ready.
    kSandboxedApiReference,
    kSandboxedExternalReference,
    kSandboxedRawExternalReference,
    // In-place weak references.
    kClearedWeakReference,
    kWeakPrefix,
    // Registers the current slot as a "pending" forward reference, to be later
    // filled by a corresponding resolution bytecode.
    kRegisterPendingForwardRef,
    // Resolves an existing "pending" forward reference to point to the current
    // object.
    kResolvePendingForwardRef,
    // Special construction bytecodes for the metamaps. In theory we could
    // reuse forward-references for this, but then the forward reference would
    // be registered during object map deserialization, before the object is
    // allocated, so there wouldn't be a allocated object whose map field we can
    // register as the pending field. We could either hack around this, or
    // simply introduce this new bytecode.
    kNewContextlessMetaMap,
    kNewContextfulMetaMap,
    // When the sandbox is enabled, a prefix indicating that the following
    // object is referenced through an indirect pointer, i.e. through an entry
    // in a pointer table.
    kIndirectPointerPrefix,
    // When the sandbox is enabled, this bytecode instructs the deserializer to
    // initialize the "self" indirect pointer of trusted objects, which
    // references the object's pointer table entry. As the "self" indirect
    // pointer is always the first field after the map word, it is guaranteed
    // that it will be deserialized before any inner objects, which may require
    // the pointer table entry for back reference to the trusted object.
    kInitializeSelfIndirectPointer,
    // This bytecode instructs the deserializer to allocate an entry in the
    // JSDispatchTable for the host object and store the corresponding dispatch
    // handle into the current slot.
    kAllocateJSDispatchEntry,
    // A back-reference to the already allocated n-th dispatch entry.
    kJSDispatchEntry,
    // A prefix indicating that the following object is referenced through a
    // protected pointer, i.e. a pointer from one trusted object to another.
    kProtectedPointerPrefix,

    //
    // ---------- byte code range 0x40..0x7f ----------
    //

    // 0x40..0x5f
    kRootArrayConstants = 0x40,

    // 0x60..0x7f
    kFixedRawData = 0x60,

    //
    // ---------- byte code range 0x80..0x9f ----------
    //

    // 0x80..0x8f
    kFixedRepeatRoot = 0x80,

    // 0x90..0x97
    kHotObject = 0x90,
  };

  // Helper class for encoding and decoding a value into and from a bytecode.
  //
  // The value is encoded by allocating an entire bytecode range, and encoding
  // the value as an index in that range, starting at kMinValue; thus the range
  // of values
  //   [kMinValue, kMinValue + 1, ... , kMaxValue]
  // is encoded as
  //   [kBytecode, kBytecode + 1, ... , kBytecode + (N - 1)]
  // where N is the number of values, i.e. kMaxValue - kMinValue + 1.
  template <Bytecode kBytecode, int kMinValue, int kMaxValue,
            typename TValue = int>
  struct BytecodeValueEncoder {
    static_assert((kBytecode + kMaxValue - kMinValue) <= kMaxUInt8);

    static constexpr bool IsEncodable(TValue value) {
      return base::IsInRange(static_cast<int>(value), kMinValue, kMaxValue);
    }

    static constexpr uint8_t Encode(TValue value) {
      DCHECK(IsEncodable(value));
      return static_cast<uint8_t>(kBytecode + static_cast<int>(value) -
                                  kMinValue);
    }

    static constexpr TValue Decode(uint8_t bytecode) {
      DCHECK(base::IsInRange(bytecode, Encode(static_cast<TValue>(kMinValue)),
                             Encode(static_cast<TValue>(kMaxValue))));
      return static_cast<TValue>(bytecode - kBytecode + kMinValue);
    }
  };

  template <Bytecode bytecode>
  using SpaceEncoder =
      BytecodeValueEncoder<bytecode, 0, kNumberOfSnapshotSpaces - 1,
                           SnapshotSpace>;

  using NewObject = SpaceEncoder<kNewObject>;

  //
  // Some other constants.
  //

  // Sentinel after a new object to indicate that double alignment is needed.
  static const int kDoubleAlignmentSentinel = 0;

  // Raw data size encoding helpers.
  static const int kFirstEncodableFixedRawDataSize = 1;
  static const int kLastEncodableFixedRawDataSize =
      kFirstEncodableFixedRawDataSize + kFixedRawDataCount - 1;

  using FixedRawDataWithSize =
      BytecodeValueEncoder<kFixedRawData, kFirstEncodableFixedRawDataSize,
                           kLastEncodableFixedRawDataSize>;

  // Repeat count encoding helpers.
  static const int kFirstEncodableRepeatRootCount = 2;
  static const int kLastEncodableFixedRepeatRootCount =
      kFirstEncodableRepeatRootCount + kFixedRepeatRootCount - 1;
  static const int kFirstEncodableVariableRepeatRootCount =
      kLastEncodableFixedRepeatRootCount + 1;

  using FixedRepeatRootWithCount =
      BytecodeValueEncoder<kFixedRepeatRoot, kFirstEncodableRepeatRootCount,
                           kLastEncodableFixedRepeatRootCount>;

  // Encodes/decodes repeat count into a serialized variable repeat count
  // value.
  struct VariableRepeatRootCount {
    static constexpr bool IsEncodable(int repeat_count) {
      return repeat_count >= kFirstEncodableVariableRepeatRootCount;
    }

    static constexpr int Encode(int repeat_count) {
      DCHECK(IsEncodable(repeat_count));
      return repeat_count - kFirstEncodableVariableRepeatRootCount;
    }

    static constexpr int Decode(int value) {
      return value + kFirstEncodableVariableRepeatRootCount;
    }
  };

  using RootArrayConstant =
      BytecodeValueEncoder<kRootArrayConstants, 0, kRootArrayConstantsCount - 1,
                           RootIndex>;
  using HotObject = BytecodeValueEncoder<kHotObject, 0, kHotObjectCount - 1>;

  // This backing store reference value represents empty backing stores during
  // serialization/deserialization.
  static const uint32_t kEmptyBackingStoreRefSentinel = 0;
};

struct SerializeEmbedderFieldsCallback {
  explicit SerializeEmbedderFieldsCallback(
      v8::SerializeInternalFieldsCallback js_cb =
          v8::SerializeInternalFieldsCallback(),
      v8::SerializeContextDataCallback context_cb =
          v8::SerializeContextDataCallback(),
      v8::SerializeAPIWrapperCallback api_wrapper_cb =
          v8::SerializeAPIWrapperCallback())
      : js_object_callback(js_cb),
        context_callback(context_cb),
        api_wrapper_callback(api_wrapper_cb) {}
  v8::SerializeInternalFieldsCallback js_object_callback;
  v8::SerializeContextDataCallback context_callback;
  v8::SerializeAPIWrapperCallback api_wrapper_callback;
};

struct DeserializeEmbedderFieldsCallback {
  explicit DeserializeEmbedderFieldsCallback(
      v8::DeserializeInternalFieldsCallback js_cb =
          v8::DeserializeInternalFieldsCallback(),
      v8::DeserializeContextDataCallback context_cb =
          v8::DeserializeContextDataCallback(),
      v8::DeserializeAPIWrapperCallback api_wrapper_cb =
          v8::DeserializeAPIWrapperCallback())
      : js_object_callback(js_cb),
        context_callback(context_cb),
        api_wrapper_callback(api_wrapper_cb) {}
  v8::DeserializeInternalFieldsCallback js_object_callback;
  v8::DeserializeContextDataCallback context_callback;
  v8::DeserializeAPIWrapperCallback api_wrapper_callback;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SERIALIZER_DESERIALIZER_H_

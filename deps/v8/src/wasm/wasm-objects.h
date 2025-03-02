// Copyright 2016 the V8 project authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_OBJECTS_H_
#define V8_WASM_WASM_OBJECTS_H_

#include <memory>
#include <optional>

#include "include/v8-wasm.h"
#include "src/base/bit-field.h"
#include "src/debug/interface-types.h"
#include "src/heap/heap.h"
#include "src/objects/backing-store.h"
#include "src/objects/casting.h"
#include "src/objects/foreign.h"
#include "src/objects/js-function.h"
#include "src/objects/js-objects.h"
#include "src/objects/objects-body-descriptors.h"
#include "src/objects/objects.h"
#include "src/objects/struct.h"
#include "src/objects/trusted-object.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/stacks.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-module.h"

// Has to be the last include (doesn't have include guards)
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {
namespace wasm {
class NativeModule;
class WasmCode;
struct WasmFunction;
struct WasmGlobal;
struct WasmModule;
struct WasmTag;
using WasmTagSig = FunctionSig;
class WasmValue;
class WireBytesRef;
}  // namespace wasm

class BreakPoint;
class JSArrayBuffer;
class SeqOneByteString;
class StructBodyDescriptor;
class WasmCapiFunction;
class WasmExceptionTag;
class WasmExportedFunction;
class WasmExternalFunction;
class WasmTrustedInstanceData;
class WasmJSFunction;
class WasmModuleObject;

enum class SharedFlag : uint8_t;

template <typename CppType>
class Managed;
template <typename CppType>
class TrustedManaged;

#include "torque-generated/src/wasm/wasm-objects-tq.inc"

#define DECL_OPTIONAL_ACCESSORS(name, type) \
  DECL_GETTER(has_##name, bool)             \
  DECL_ACCESSORS(name, type)

class V8_EXPORT_PRIVATE FunctionTargetAndImplicitArg {
 public:
  FunctionTargetAndImplicitArg(
      Isolate* isolate, Handle<WasmTrustedInstanceData> target_instance_data,
      int target_func_index);
  // The "implicit_arg" will be a WasmTrustedInstanceData or a WasmImportData.
  DirectHandle<TrustedObject> implicit_arg() { return implicit_arg_; }
  WasmCodePointer call_target() { return call_target_; }

#if V8_ENABLE_DRUMBRAKE
  int target_func_index() { return target_func_index_; }
#endif  // V8_ENABLE_DRUMBRAKE

 private:
  Handle<TrustedObject> implicit_arg_;
  WasmCodePointer call_target_;

#if V8_ENABLE_DRUMBRAKE
  int target_func_index_;
#endif  // V8_ENABLE_DRUMBRAKE
};

namespace wasm {
enum class OnResume : int { kContinue, kThrow };
}  // namespace wasm

// A helper for an entry for an imported function, indexed statically.
// The underlying storage in the instance is used by generated code to
// call imported functions at runtime.
// Each entry is either:
//   - Wasm to JS, which has fields
//      - object = a WasmImportData
//      - target = entrypoint to import wrapper code
//   - Wasm to Wasm, which has fields
//      - object = target instance data
//      - target = entrypoint for the function
class ImportedFunctionEntry {
 public:
  inline ImportedFunctionEntry(Handle<WasmTrustedInstanceData>, int index);

  // Initialize this entry as a Wasm to JS call. This accepts the isolate as a
  // parameter since it allocates a WasmImportData.
  void SetGenericWasmToJs(Isolate*, DirectHandle<JSReceiver> callable,
                          wasm::Suspend suspend, const wasm::CanonicalSig* sig,
                          wasm::CanonicalTypeIndex sig_id);
  V8_EXPORT_PRIVATE void SetCompiledWasmToJs(Isolate*,
                                             DirectHandle<JSReceiver> callable,
                                             wasm::WasmCode* wasm_to_js_wrapper,
                                             wasm::Suspend suspend,
                                             const wasm::CanonicalSig* sig,
                                             wasm::CanonicalTypeIndex sig_id);

  // Initialize this entry as a Wasm to Wasm call.
  void SetWasmToWasm(Tagged<WasmTrustedInstanceData> target_instance_object,
                     WasmCodePointer call_target,
                     wasm::CanonicalTypeIndex sig_id
#if V8_ENABLE_DRUMBRAKE
                     ,
                     int exported_function_index
#endif  // V8_ENABLE_DRUMBRAKE
  );

  Tagged<JSReceiver> callable();
  Tagged<Object> maybe_callable();
  Tagged<Object> implicit_arg();
  WasmCodePointer target();

#if V8_ENABLE_DRUMBRAKE
  int function_index_in_called_module();
#endif  // V8_ENABLE_DRUMBRAKE

 private:
  Handle<WasmTrustedInstanceData> const instance_data_;
  int const index_;
};

enum InternalizeString : bool { kInternalize = true, kNoInternalize = false };

// Representation of a WebAssembly.Module JavaScript-level object.
class WasmModuleObject
    : public TorqueGeneratedWasmModuleObject<WasmModuleObject, JSObject> {
 public:
  inline wasm::NativeModule* native_module() const;
  inline const std::shared_ptr<wasm::NativeModule>& shared_native_module()
      const;
  inline const wasm::WasmModule* module() const;

  // Dispatched behavior.
  DECL_PRINTER(WasmModuleObject)

  // Creates a new {WasmModuleObject} for an existing {NativeModule} that is
  // reference counted and might be shared between multiple Isolates.
  V8_EXPORT_PRIVATE static Handle<WasmModuleObject> New(
      Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
      DirectHandle<Script> script);

  // Check whether this module was generated from asm.js source.
  inline bool is_asm_js();

  // Get the module name, if set. Returns an empty handle otherwise.
  static MaybeDirectHandle<String> GetModuleNameOrNull(
      Isolate*, DirectHandle<WasmModuleObject>);

  // Get the function name of the function identified by the given index.
  // Returns a null handle if the function is unnamed or the name is not a valid
  // UTF-8 string.
  static MaybeHandle<String> GetFunctionNameOrNull(
      Isolate*, DirectHandle<WasmModuleObject>, uint32_t func_index);

  // Get the raw bytes of the function name of the function identified by the
  // given index.
  // Meant to be used for debugging or frame printing.
  // Does not allocate, hence gc-safe.
  base::Vector<const uint8_t> GetRawFunctionName(int func_index);

  // Extract a portion of the wire bytes as UTF-8 string, optionally
  // internalized. (Prefer to internalize early if the string will be used for a
  // property lookup anyway.)
  static Handle<String> ExtractUtf8StringFromModuleBytes(
      Isolate*, DirectHandle<WasmModuleObject>, wasm::WireBytesRef,
      InternalizeString);
  static Handle<String> ExtractUtf8StringFromModuleBytes(
      Isolate*, base::Vector<const uint8_t> wire_byte, wasm::WireBytesRef,
      InternalizeString);

  TQ_OBJECT_CONSTRUCTORS(WasmModuleObject)
};

#if V8_ENABLE_SANDBOX || DEBUG
// This should be checked on all code paths that write into WasmDispatchTables.
bool FunctionSigMatchesTable(wasm::CanonicalTypeIndex sig_id,
                             wasm::CanonicalValueType table_type);
#endif

// Representation of a WebAssembly.Table JavaScript-level object.
class WasmTableObject
    : public TorqueGeneratedWasmTableObject<WasmTableObject, JSObject> {
 public:
  class BodyDescriptor;

  inline wasm::ValueType type(const wasm::WasmModule* module);
  inline wasm::CanonicalValueType canonical_type(
      const wasm::WasmModule* module);
  // Use this when you don't care whether the type stored on the in-sandbox
  // object might have been corrupted to contain an invalid type index.
  // That implies that you can't even canonicalize the type!
  inline wasm::ValueType unsafe_type();

  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)
  DECL_TRUSTED_POINTER_ACCESSORS(trusted_dispatch_table, WasmDispatchTable)

  DECL_VERIFIER(WasmTableObject)

  V8_EXPORT_PRIVATE static int Grow(Isolate* isolate,
                                    DirectHandle<WasmTableObject> table,
                                    uint32_t count,
                                    DirectHandle<Object> init_value);

  // TODO(jkummerow): Consider getting rid of {type}, use {canonical_type}
  // instead.
  V8_EXPORT_PRIVATE static Handle<WasmTableObject> New(
      Isolate* isolate, DirectHandle<WasmTrustedInstanceData> trusted_data,
      wasm::ValueType type, wasm::CanonicalValueType canonical_type,
      uint32_t initial, bool has_maximum, uint64_t maximum,
      DirectHandle<Object> initial_value, wasm::AddressType address_type);

  inline bool is_in_bounds(uint32_t entry_index);

  inline bool is_table64() const;

  // Get the declared maximum as uint64_t or nullopt if no maximum was declared.
  inline std::optional<uint64_t> maximum_length_u64() const;

  // Thin wrapper around {JsToWasmObject}.
  static MaybeHandle<Object> JSToWasmElement(
      Isolate* isolate, DirectHandle<WasmTableObject> table,
      Handle<Object> entry, const char** error_message);

  // This function will not handle JS objects; i.e., {entry} needs to be in wasm
  // representation.
  V8_EXPORT_PRIVATE static void Set(Isolate* isolate,
                                    DirectHandle<WasmTableObject> table,
                                    uint32_t index, DirectHandle<Object> entry);

  V8_EXPORT_PRIVATE static Handle<Object> Get(
      Isolate* isolate, DirectHandle<WasmTableObject> table, uint32_t index);

  V8_EXPORT_PRIVATE static void Fill(Isolate* isolate,
                                     DirectHandle<WasmTableObject> table,
                                     uint32_t start, DirectHandle<Object> entry,
                                     uint32_t count);

  // TODO(wasm): Unify these three methods into one.
  static void UpdateDispatchTable(
      Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
      const wasm::WasmFunction* func,
      DirectHandle<WasmTrustedInstanceData> target_instance
#if V8_ENABLE_DRUMBRAKE
      ,
      int target_func_index
#endif  // V8_ENABLE_DRUMBRAKE
  );
  static void UpdateDispatchTable(Isolate* isolate,
                                  DirectHandle<WasmTableObject> table,
                                  int entry_index,
                                  DirectHandle<WasmJSFunction> function);
  static void UpdateDispatchTable(Isolate* isolate,
                                  DirectHandle<WasmTableObject> table,
                                  int entry_index,
                                  DirectHandle<WasmCapiFunction> capi_function);

  void ClearDispatchTable(int index);

  V8_EXPORT_PRIVATE static void SetFunctionTablePlaceholder(
      Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      int func_index);

  // This function reads the content of a function table entry and returns it
  // through the output parameters.
  static void GetFunctionTableEntry(
      Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
      bool* is_valid, bool* is_null,
      MaybeHandle<WasmTrustedInstanceData>* instance_data, int* function_index,
      MaybeDirectHandle<WasmJSFunction>* maybe_js_function);

 private:
  // {entry} is either {Null} or a {WasmInternalFunction}.
  static void SetFunctionTableEntry(Isolate* isolate,
                                    DirectHandle<WasmTableObject> table,
                                    int entry_index,
                                    DirectHandle<Object> entry);

  TQ_OBJECT_CONSTRUCTORS(WasmTableObject)
};

class WasmMemoryMapDescriptor
    : public TorqueGeneratedWasmMemoryMapDescriptor<WasmMemoryMapDescriptor,
                                                    JSObject> {
 public:
  V8_EXPORT_PRIVATE static MaybeDirectHandle<WasmMemoryMapDescriptor>
  NewFromAnonymous(Isolate* isolate, size_t length);

  V8_EXPORT_PRIVATE static DirectHandle<WasmMemoryMapDescriptor>
  NewFromFileDescriptor(
      Isolate* isolate,
      v8::WasmMemoryMapDescriptor::WasmFileDescriptor file_descriptor);

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmMemoryMapDescriptor)
};

// Representation of a WebAssembly.Memory JavaScript-level object.
class WasmMemoryObject
    : public TorqueGeneratedWasmMemoryObject<WasmMemoryObject, JSObject> {
 public:
  class BodyDescriptor;

  DECL_ACCESSORS(instances, Tagged<WeakArrayList>)

  // Add a use of this memory object to the given instance. This updates the
  // internal weak list of instances that use this memory and also updates the
  // fields of the instance to reference this memory's buffer.
  // Note that we update both the non-shared and shared (if any) parts of the
  // instance for faster access to shared memory.
  V8_EXPORT_PRIVATE static void UseInInstance(
      Isolate* isolate, DirectHandle<WasmMemoryObject> memory,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      DirectHandle<WasmTrustedInstanceData> shared_trusted_instance_data,
      int memory_index_in_instance);
  inline bool has_maximum_pages();

  inline bool is_memory64() const;

  V8_EXPORT_PRIVATE static Handle<WasmMemoryObject> New(
      Isolate* isolate, DirectHandle<JSArrayBuffer> buffer, int maximum,
      wasm::AddressType address_type);

  V8_EXPORT_PRIVATE static MaybeHandle<WasmMemoryObject> New(
      Isolate* isolate, int initial, int maximum, SharedFlag shared,
      wasm::AddressType address_type);

  // Assign a new (grown) buffer to this memory, also updating the shortcut
  // fields of all instances that use this memory.
  void SetNewBuffer(Tagged<JSArrayBuffer> new_buffer);

  V8_EXPORT_PRIVATE static int32_t Grow(Isolate*,
                                        DirectHandle<WasmMemoryObject>,
                                        uint32_t pages);

  // Returns the number of bytes that got mapped into the WebAssembly.Memory.
  V8_EXPORT_PRIVATE size_t MapDescriptor(
      DirectHandle<WasmMemoryMapDescriptor> descriptor, size_t offset);

  static constexpr int kNoMaximum = -1;

  TQ_OBJECT_CONSTRUCTORS(WasmMemoryObject)
};

// Representation of a WebAssembly.Global JavaScript-level object.
class WasmGlobalObject
    : public TorqueGeneratedWasmGlobalObject<WasmGlobalObject, JSObject> {
 public:
  class BodyDescriptor;

  DECL_ACCESSORS(untagged_buffer, Tagged<JSArrayBuffer>)
  DECL_ACCESSORS(tagged_buffer, Tagged<FixedArray>)
  DECL_PRIMITIVE_ACCESSORS(type, wasm::ValueType)
  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)

  // Dispatched behavior.
  DECL_PRINTER(WasmGlobalObject)

  V8_EXPORT_PRIVATE static MaybeHandle<WasmGlobalObject> New(
      Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance_object,
      MaybeDirectHandle<JSArrayBuffer> maybe_untagged_buffer,
      MaybeDirectHandle<FixedArray> maybe_tagged_buffer, wasm::ValueType type,
      int32_t offset, bool is_mutable);

  inline int type_size() const;

  inline int32_t GetI32();
  inline int64_t GetI64();
  inline float GetF32();
  inline double GetF64();
  inline uint8_t* GetS128RawBytes();
  inline Handle<Object> GetRef();

  inline void SetI32(int32_t value);
  inline void SetI64(int64_t value);
  inline void SetF32(float value);
  inline void SetF64(double value);
  // {value} must be an object in Wasm representation.
  inline void SetRef(DirectHandle<Object> value);

 private:
  // This function returns the address of the global's data in the
  // JSArrayBuffer. This buffer may be allocated on-heap, in which case it may
  // not have a fixed address.
  inline Address address() const;

  TQ_OBJECT_CONSTRUCTORS(WasmGlobalObject)
};

// The trusted part of a WebAssembly instance.
// This object lives in trusted space and is never modified from user space.
class V8_EXPORT_PRIVATE WasmTrustedInstanceData : public ExposedTrustedObject {
 public:
  DECL_OPTIONAL_ACCESSORS(instance_object, Tagged<WasmInstanceObject>)
  DECL_ACCESSORS(native_context, Tagged<Context>)
  DECL_ACCESSORS(memory_objects, Tagged<FixedArray>)
#if V8_ENABLE_DRUMBRAKE
  DECL_OPTIONAL_ACCESSORS(interpreter_object, Tagged<Tuple2>)
#endif  // V8_ENABLE_DRUMBRAKE
  DECL_OPTIONAL_ACCESSORS(untagged_globals_buffer, Tagged<JSArrayBuffer>)
  DECL_OPTIONAL_ACCESSORS(tagged_globals_buffer, Tagged<FixedArray>)
  DECL_OPTIONAL_ACCESSORS(imported_mutable_globals_buffers, Tagged<FixedArray>)
  // tables: FixedArray of WasmTableObject.
  DECL_OPTIONAL_ACCESSORS(tables, Tagged<FixedArray>)
  DECL_PROTECTED_POINTER_ACCESSORS(dispatch_table_for_imports,
                                   WasmDispatchTable)
  DECL_ACCESSORS(imported_mutable_globals, Tagged<FixedAddressArray>)
#if V8_ENABLE_DRUMBRAKE
  // Points to an array that contains the function index for each imported Wasm
  // function. This is required to call imported functions from the Wasm
  // interpreter.
  DECL_ACCESSORS(imported_function_indices, Tagged<FixedInt32Array>)
#endif  // V8_ENABLE_DRUMBRAKE
  DECL_PROTECTED_POINTER_ACCESSORS(shared_part, WasmTrustedInstanceData)
  DECL_PROTECTED_POINTER_ACCESSORS(dispatch_table0, WasmDispatchTable)
  DECL_PROTECTED_POINTER_ACCESSORS(dispatch_tables, ProtectedFixedArray)
  DECL_OPTIONAL_ACCESSORS(tags_table, Tagged<FixedArray>)
  DECL_ACCESSORS(func_refs, Tagged<FixedArray>)
  DECL_ACCESSORS(managed_object_maps, Tagged<FixedArray>)
  DECL_ACCESSORS(feedback_vectors, Tagged<FixedArray>)
  DECL_ACCESSORS(well_known_imports, Tagged<FixedArray>)
  DECL_PRIMITIVE_ACCESSORS(memory0_start, uint8_t*)
  DECL_PRIMITIVE_ACCESSORS(memory0_size, size_t)
  DECL_PROTECTED_POINTER_ACCESSORS(managed_native_module,
                                   TrustedManaged<wasm::NativeModule>)
  DECL_PRIMITIVE_ACCESSORS(new_allocation_limit_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(new_allocation_top_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(old_allocation_limit_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(old_allocation_top_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(globals_start, uint8_t*)
  DECL_PRIMITIVE_ACCESSORS(jump_table_start, Address)
  DECL_PRIMITIVE_ACCESSORS(hook_on_function_call_address, Address)
  DECL_PRIMITIVE_ACCESSORS(tiering_budget_array, std::atomic<uint32_t>*)
  DECL_PROTECTED_POINTER_ACCESSORS(memory_bases_and_sizes,
                                   TrustedFixedAddressArray)
  DECL_ACCESSORS(data_segment_starts, Tagged<FixedAddressArray>)
  DECL_ACCESSORS(data_segment_sizes, Tagged<FixedUInt32Array>)
  DECL_ACCESSORS(element_segments, Tagged<FixedArray>)
  DECL_PRIMITIVE_ACCESSORS(break_on_entry, uint8_t)
  DECL_PRIMITIVE_ACCESSORS(stress_deopt_counter_address, Address)

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic. Depending on the V8 build mode there could be no padding.
  inline void clear_padding();

  inline Tagged<WasmMemoryObject> memory_object(int memory_index) const;
  inline uint8_t* memory_base(int memory_index) const;
  inline size_t memory_size(int memory_index) const;

  inline wasm::NativeModule* native_module() const;

  inline Tagged<WasmModuleObject> module_object() const;
  inline const wasm::WasmModule* module() const;

  // Dispatched behavior.
  DECL_PRINTER(WasmTrustedInstanceData)
  DECL_VERIFIER(WasmTrustedInstanceData)

// Layout description.
#define FIELD_LIST(V)                                                     \
  /* Often-accessed fields go first to minimize generated code size. */   \
  /* Less than system pointer sized fields come first. */                 \
  V(kProtectedDispatchTable0Offset, kTaggedSize)                          \
  V(kProtectedDispatchTableForImportsOffset, kTaggedSize)                 \
  V(kImportedMutableGlobalsOffset, kTaggedSize)                           \
  IF_WASM_DRUMBRAKE(V, kImportedFunctionIndicesOffset, kTaggedSize)       \
  /* Optional padding to align system pointer size fields */              \
  V(kOptionalPaddingOffset, POINTER_SIZE_PADDING(kOptionalPaddingOffset)) \
  V(kMemory0StartOffset, kSystemPointerSize)                              \
  V(kMemory0SizeOffset, kSizetSize)                                       \
  V(kGlobalsStartOffset, kSystemPointerSize)                              \
  V(kJumpTableStartOffset, kSystemPointerSize)                            \
  /* End of often-accessed fields. */                                     \
  /* Continue with system pointer size fields to maintain alignment. */   \
  V(kNewAllocationLimitAddressOffset, kSystemPointerSize)                 \
  V(kNewAllocationTopAddressOffset, kSystemPointerSize)                   \
  V(kOldAllocationLimitAddressOffset, kSystemPointerSize)                 \
  V(kOldAllocationTopAddressOffset, kSystemPointerSize)                   \
  V(kHookOnFunctionCallAddressOffset, kSystemPointerSize)                 \
  V(kTieringBudgetArrayOffset, kSystemPointerSize)                        \
  V(kStressDeoptCounterOffset, kSystemPointerSize)                        \
  /* Less than system pointer size aligned fields are below. */           \
  V(kProtectedMemoryBasesAndSizesOffset, kTaggedSize)                     \
  V(kDataSegmentStartsOffset, kTaggedSize)                                \
  V(kDataSegmentSizesOffset, kTaggedSize)                                 \
  V(kElementSegmentsOffset, kTaggedSize)                                  \
  V(kInstanceObjectOffset, kTaggedSize)                                   \
  V(kNativeContextOffset, kTaggedSize)                                    \
  V(kProtectedSharedPartOffset, kTaggedSize)                              \
  V(kMemoryObjectsOffset, kTaggedSize)                                    \
  V(kUntaggedGlobalsBufferOffset, kTaggedSize)                            \
  V(kTaggedGlobalsBufferOffset, kTaggedSize)                              \
  V(kImportedMutableGlobalsBuffersOffset, kTaggedSize)                    \
  IF_WASM_DRUMBRAKE(V, kInterpreterObjectOffset, kTaggedSize)             \
  V(kTablesOffset, kTaggedSize)                                           \
  V(kProtectedDispatchTablesOffset, kTaggedSize)                          \
  V(kTagsTableOffset, kTaggedSize)                                        \
  V(kFuncRefsOffset, kTaggedSize)                                         \
  V(kManagedObjectMapsOffset, kTaggedSize)                                \
  V(kFeedbackVectorsOffset, kTaggedSize)                                  \
  V(kWellKnownImportsOffset, kTaggedSize)                                 \
  V(kProtectedManagedNativeModuleOffset, kTaggedSize)                     \
  V(kBreakOnEntryOffset, kUInt8Size)                                      \
  /* More padding to make the header pointer-size aligned */              \
  V(kHeaderPaddingOffset, POINTER_SIZE_PADDING(kHeaderPaddingOffset))     \
  V(kHeaderSize, 0)                                                       \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(ExposedTrustedObject::kHeaderSize, FIELD_LIST)
  static_assert(IsAligned(kHeaderSize, kTaggedSize));
  // TODO(ishell, v8:8875): When pointer compression is enabled 8-byte size
  // fields (external pointers, doubles and BigInt data) are only kTaggedSize
  // aligned so checking for alignments of fields bigger than kTaggedSize
  // doesn't make sense until v8:8875 is fixed.
#define ASSERT_FIELD_ALIGNED(offset, size)                                 \
  static_assert(size == 0 || IsAligned(offset, size) ||                    \
                (COMPRESS_POINTERS_BOOL && (size == kSystemPointerSize) && \
                 IsAligned(offset, kTaggedSize)));
  FIELD_LIST(ASSERT_FIELD_ALIGNED)
#undef ASSERT_FIELD_ALIGNED
#undef FIELD_LIST

  // GC support: List all tagged fields and protected fields.
  // V(offset, name)
#define WASM_TAGGED_INSTANCE_DATA_FIELDS(V)                                   \
  V(kInstanceObjectOffset, "instance_object")                                 \
  V(kNativeContextOffset, "native_context")                                   \
  V(kMemoryObjectsOffset, "memory_objects")                                   \
  V(kUntaggedGlobalsBufferOffset, "untagged_globals_buffer")                  \
  V(kTaggedGlobalsBufferOffset, "tagged_globals_buffer")                      \
  V(kImportedMutableGlobalsBuffersOffset, "imported_mutable_globals_buffers") \
  IF_WASM_DRUMBRAKE(V, kInterpreterObjectOffset, "interpreter_object")        \
  V(kTablesOffset, "tables")                                                  \
  V(kTagsTableOffset, "tags_table")                                           \
  V(kFuncRefsOffset, "func_refs")                                             \
  V(kManagedObjectMapsOffset, "managed_object_maps")                          \
  V(kFeedbackVectorsOffset, "feedback_vectors")                               \
  V(kWellKnownImportsOffset, "well_known_imports")                            \
  V(kImportedMutableGlobalsOffset, "imported_mutable_globals")                \
  IF_WASM_DRUMBRAKE(V, kImportedFunctionIndicesOffset,                        \
                    "imported_function_indices")                              \
  V(kDataSegmentStartsOffset, "data_segment_starts")                          \
  V(kDataSegmentSizesOffset, "data_segment_sizes")                            \
  V(kElementSegmentsOffset, "element_segments")
#define WASM_PROTECTED_INSTANCE_DATA_FIELDS(V)                             \
  V(kProtectedSharedPartOffset, "shared_part")                             \
  V(kProtectedMemoryBasesAndSizesOffset, "memory_bases_and_sizes")         \
  V(kProtectedDispatchTable0Offset, "dispatch_table0")                     \
  V(kProtectedDispatchTablesOffset, "dispatch_tables")                     \
  V(kProtectedDispatchTableForImportsOffset, "dispatch_table_for_imports") \
  V(kProtectedManagedNativeModuleOffset, "managed_native_module")

#define WASM_INSTANCE_FIELD_OFFSET(offset, _) offset,
#define WASM_INSTANCE_FIELD_NAME(_, name) name,

#if V8_ENABLE_DRUMBRAKE
  static constexpr size_t kWasmInterpreterAdditionalFields = 2;
#else
  static constexpr size_t kWasmInterpreterAdditionalFields = 0;
#endif  // V8_ENABLE_DRUMBRAKE
  static constexpr size_t kTaggedFieldsCount =
      16 + kWasmInterpreterAdditionalFields;

  static constexpr std::array<uint16_t, kTaggedFieldsCount>
      kTaggedFieldOffsets = {
          WASM_TAGGED_INSTANCE_DATA_FIELDS(WASM_INSTANCE_FIELD_OFFSET)};
  static constexpr std::array<const char*, kTaggedFieldsCount>
      kTaggedFieldNames = {
          WASM_TAGGED_INSTANCE_DATA_FIELDS(WASM_INSTANCE_FIELD_NAME)};
  static constexpr std::array<uint16_t, 6> kProtectedFieldOffsets = {
      WASM_PROTECTED_INSTANCE_DATA_FIELDS(WASM_INSTANCE_FIELD_OFFSET)};
  static constexpr std::array<const char*, 6> kProtectedFieldNames = {
      WASM_PROTECTED_INSTANCE_DATA_FIELDS(WASM_INSTANCE_FIELD_NAME)};

#undef WASM_INSTANCE_FIELD_OFFSET
#undef WASM_INSTANCE_FIELD_NAME
#undef WASM_TAGGED_INSTANCE_DATA_FIELDS
#undef WASM_PROTECTED_INSTANCE_DATA_FIELDS

  static_assert(kTaggedFieldOffsets.size() == kTaggedFieldNames.size(),
                "every tagged field offset needs a name");
  static_assert(kProtectedFieldOffsets.size() == kProtectedFieldNames.size(),
                "every protected field offset needs a name");

  void SetRawMemory(int memory_index, uint8_t* mem_start, size_t mem_size);

#if V8_ENABLE_DRUMBRAKE
  // Get the interpreter object associated with the given wasm object.
  // If no interpreter object exists yet, it is created automatically.
  static Handle<Tuple2> GetOrCreateInterpreterObject(
      Handle<WasmInstanceObject>);
  static Handle<Tuple2> GetInterpreterObject(Handle<WasmInstanceObject>);
#endif  // V8_ENABLE_DRUMBRAKE

  static Handle<WasmTrustedInstanceData> New(Isolate*,
                                             DirectHandle<WasmModuleObject>,
                                             bool shared);

  WasmCodePointer GetCallTarget(uint32_t func_index);

  inline Tagged<WasmDispatchTable> dispatch_table(uint32_t table_index);
  inline bool has_dispatch_table(uint32_t table_index);

  // Copies table entries. Returns {false} if the ranges are out-of-bounds.
  static bool CopyTableEntries(
      Isolate* isolate,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      uint32_t table_dst_index, uint32_t table_src_index, uint32_t dst,
      uint32_t src, uint32_t count) V8_WARN_UNUSED_RESULT;

  // Loads a range of elements from element segment into a table.
  // Returns the empty {Optional} if the operation succeeds, or an {Optional}
  // with the error {MessageTemplate} if it fails.
  static std::optional<MessageTemplate> InitTableEntries(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
      Handle<WasmTrustedInstanceData> shared_trusted_instance_data,
      uint32_t table_index, uint32_t segment_index, uint32_t dst, uint32_t src,
      uint32_t count) V8_WARN_UNUSED_RESULT;

  class BodyDescriptor;

  // Read a WasmFuncRef from the func_refs FixedArray. Returns true on success
  // and writes the result in the output parameter. Returns false if no func_ref
  // exists yet for this function. Use GetOrCreateFuncRef to always create one.
  bool try_get_func_ref(int index, Tagged<WasmFuncRef>* result);

  // Acquires the {WasmFuncRef} for a given {function_index} from the cache of
  // the given {trusted_instance_data}, or creates a new {WasmInternalFunction}
  // and {WasmFuncRef} if it does not exist yet. The new objects are added to
  // the cache of the {trusted_instance_data} immediately.
  static Handle<WasmFuncRef> GetOrCreateFuncRef(
      Isolate* isolate,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      int function_index);

  // Get a raw pointer to the location where the given global is stored.
  // {global} must not be a reference type.
  uint8_t* GetGlobalStorage(const wasm::WasmGlobal&);

  // Get the FixedArray and the index in that FixedArray for the given global,
  // which must be a reference type.
  std::pair<Tagged<FixedArray>, uint32_t> GetGlobalBufferAndIndex(
      const wasm::WasmGlobal&);

  // Get the value of a global.
  wasm::WasmValue GetGlobalValue(Isolate*, const wasm::WasmGlobal&);

  OBJECT_CONSTRUCTORS(WasmTrustedInstanceData, ExposedTrustedObject);

 private:
  void InitDataSegmentArrays(const wasm::NativeModule*);
};

// Representation of a WebAssembly.Instance JavaScript-level object.
// This is mostly a wrapper around the WasmTrustedInstanceData, plus any
// user-set properties.
class WasmInstanceObject
    : public TorqueGeneratedWasmInstanceObject<WasmInstanceObject, JSObject> {
 public:
  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)

  inline const wasm::WasmModule* module() const;

  class BodyDescriptor;

  DECL_PRINTER(WasmInstanceObject)
  TQ_OBJECT_CONSTRUCTORS(WasmInstanceObject)
};

// Representation of WebAssembly.Exception JavaScript-level object.
class WasmTagObject
    : public TorqueGeneratedWasmTagObject<WasmTagObject, JSObject> {
 public:
  class BodyDescriptor;

  // Checks whether the given {sig} has the same parameter types as the
  // serialized signature stored within this tag object.
  bool MatchesSignature(wasm::CanonicalTypeIndex expected_index);

  static Handle<WasmTagObject> New(
      Isolate* isolate, const wasm::FunctionSig* sig,
      wasm::CanonicalTypeIndex type_index, DirectHandle<HeapObject> tag,
      DirectHandle<WasmTrustedInstanceData> instance);

  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)

  TQ_OBJECT_CONSTRUCTORS(WasmTagObject)
};

// Off-heap data object owned by a WasmDispatchTable. Currently used for
// tracking referenced WasmToJS wrappers (shared per process), so we can
// decrement their refcounts when the WasmDispatchTable is freed.
class WasmDispatchTableData {
 public:
  WasmDispatchTableData() = default;
  ~WasmDispatchTableData();

  // This class tracks wrapper entries since it owns the corresponding
  // CodePointerTable entries. This function can be used to check if a given
  // entry points to a wrapper.
  V8_EXPORT_PRIVATE bool IsAWrapper(int index) const;

#ifdef DEBUG
  WasmCodePointer WrapperCodePointerForDebugging(int index);
#endif

 private:
  friend class WasmDispatchTable;

  // This class owns the CodePointerTable entries for generic and compiled
  // wrappers. This function adds an entry for a wrapper. If {compiled_wrapper}
  // is nullptr, the entry is for the generic wrapper.
  // The CodePointerTableEntry is reused for the generic and compiled wrapper.
  WasmCodePointer Add(int index, Address call_target,
                      wasm::WasmCode* compiled_wrapper,
                      uint64_t signature_hash);
  void Remove(int index, WasmCodePointer call_target);

  // The {wrappers_} data structure tracks installed wrappers, both generic
  // ({code} is nullptr) and compiled. It owns the CodePointerTable entry in
  // {call_target} and manages the {code} lifetime by incrementing and
  // decrementing the ref count as needed.
  struct WrapperEntry {
    WasmCodePointer call_target;
    wasm::WasmCode* code;  // {nullptr} if this is the generic wrapper.
  };
  std::unordered_map<int, WrapperEntry> wrappers_;
};

// The dispatch table is referenced from a WasmTableObject and from every
// WasmTrustedInstanceData which uses the table. It is used from generated code
// for executing indirect calls.
class WasmDispatchTable : public ExposedTrustedObject {
 public:
#if V8_ENABLE_DRUMBRAKE
  static const uint32_t kInvalidFunctionIndex = UINT_MAX;
#endif  // V8_ENABLE_DRUMBRAKE

  enum NewOrExistingEntry : bool { kNewEntry, kExistingEntry };

  class BodyDescriptor;

  static constexpr size_t kLengthOffset = kHeaderSize;
  static constexpr size_t kCapacityOffset = kLengthOffset + kUInt32Size;
  static constexpr size_t kProtectedOffheapDataOffset =
      kCapacityOffset + kUInt32Size;
  static constexpr size_t kProtectedUsesOffset =
      kProtectedOffheapDataOffset + kTaggedSize;
  static constexpr size_t kTableTypeOffset = kProtectedUsesOffset + kTaggedSize;
  static constexpr size_t kPaddingSize = TAGGED_SIZE_8_BYTES ? kUInt32Size : 0;
  static constexpr size_t kEntriesOffset =
      kTableTypeOffset + kUInt32Size + kPaddingSize;

  // Entries consist of
  // - target (pointer)
#if V8_ENABLE_DRUMBRAKE
  // - function_index (uint32_t) (located in place of target pointer).
#endif  // V8_ENABLE_DRUMBRAKE
  // - implicit_arg (protected pointer, tagged sized)
  // - sig (int32_t); unused for imports which check the signature statically.
  static constexpr size_t kTargetBias = 0;
#if V8_ENABLE_DRUMBRAKE
  // In jitless mode, reuse the 'target' field storage to hold the (uint32_t)
  // function index.
  static constexpr size_t kFunctionIndexBias = kTargetBias;
#endif  // V8_ENABLE_DRUMBRAKE
  static constexpr size_t kImplicitArgBias = kTargetBias + kSystemPointerSize;
  static constexpr size_t kSigBias = kImplicitArgBias + kTaggedSize;
  static constexpr size_t kEntryPaddingOffset = kSigBias + kInt32Size;
  static constexpr size_t kEntryPaddingBytes =
      kEntryPaddingOffset % kTaggedSize;
  static_assert(kEntryPaddingBytes == 4 || kEntryPaddingBytes == 0);
  static constexpr size_t kEntrySize = kEntryPaddingOffset + kEntryPaddingBytes;

  // Tagged and system-pointer-sized fields must be tagged-size-aligned.
  static_assert(IsAligned(kEntriesOffset, kTaggedSize));
  static_assert(IsAligned(kEntrySize, kTaggedSize));
  static_assert(IsAligned(kTargetBias, kTaggedSize));
  static_assert(IsAligned(kImplicitArgBias, kTaggedSize));

  // TODO(clemensb): If we ever enable allocation alignment we will needs to add
  // more padding to make the "target" fields system-pointer-size aligned.
  static_assert(!USE_ALLOCATION_ALIGNMENT_BOOL);

  // The total byte size must still fit in an integer.
  static constexpr int kMaxLength = (kMaxInt - kEntriesOffset) / kEntrySize;

  static constexpr int SizeFor(int length) {
    DCHECK_LE(length, kMaxLength);
    return kEntriesOffset + length * kEntrySize;
  }

  static constexpr int OffsetOf(int index) {
    DCHECK_LT(index, kMaxLength);
    return SizeFor(index);
  }

  // Clear uninitialized padding space for deterministic object content.
  // Depending on the V8 build mode there could be no padding.
  inline void clear_entry_padding(int index);

  // The current length of this dispatch table. This is always <= the capacity.
  inline int length() const;
  inline int length(AcquireLoadTag) const;
  // The current capacity. Can be bigger than the current length to allow for
  // more efficient growing.
  inline int capacity() const;

  DECL_PROTECTED_POINTER_ACCESSORS(protected_offheap_data,
                                   TrustedManaged<WasmDispatchTableData>)
  inline WasmDispatchTableData* offheap_data() const;

  // Stores all WasmTrustedInstanceData that refer to this WasmDispatchTable.
  DECL_PROTECTED_POINTER_ACCESSORS(protected_uses, ProtectedWeakFixedArray)

  // Stores the canonical type of the table.
  DECL_PRIMITIVE_ACCESSORS(table_type, wasm::CanonicalValueType)

  // Accessors.
  // {implicit_arg} will be a WasmImportData, a WasmTrustedInstanceData, or
  // Smi::zero() (if the entry was cleared).
  inline Tagged<Object> implicit_arg(int index) const;
  inline WasmCodePointer target(int index) const;
  inline wasm::CanonicalTypeIndex sig(int index) const;

  // Set an entry for indirect calls that don't go to a WasmToJS wrapper.
  // Wrappers are special since we own the CPT entries for the wrappers.
  // {implicit_arg} has to be a WasmImportData, a WasmTrustedInstanceData, or
  // Smi::zero().
  void V8_EXPORT_PRIVATE SetForNonWrapper(int index,
                                          Tagged<Object> implicit_arg,
                                          WasmCodePointer call_target,
                                          wasm::CanonicalTypeIndex sig_id,
#if V8_ENABLE_DRUMBRAKE
                                          uint32_t function_index,
#endif  // V8_ENABLE_DRUMBRAKE
                                          NewOrExistingEntry new_or_existing);

  // Set an entry for indirect calls to a WasmToJS wrapper.
  // {implicit_arg} has to be a WasmImportData, a WasmTrustedInstanceData.
  // {compiled_wrapper} needs to be set to the corresponding WasmCode, or
  // nullptr in case of the generic wrapper.
  void V8_EXPORT_PRIVATE SetForWrapper(int index, Tagged<Object> implicit_arg,
                                       Address call_target,
                                       wasm::CanonicalTypeIndex sig_id,
                                       uint64_t signature_hash,
#if V8_ENABLE_DRUMBRAKE
                                       uint32_t function_index,
#endif  // V8_ENABLE_DRUMBRAKE
                                       wasm::WasmCode* compiled_wrapper,
                                       NewOrExistingEntry new_or_existing);

#if V8_ENABLE_DRUMBRAKE
  inline uint32_t function_index(int index) const;
#endif  // V8_ENABLE_DRUMBRAKE

  void Clear(int index, NewOrExistingEntry new_or_existing);
  void InstallCompiledWrapper(int index, wasm::WasmCode* wrapper);

  bool V8_EXPORT_PRIVATE IsAWrapper(int index) const;

  static void V8_EXPORT_PRIVATE
  AddUse(Isolate* isolate, DirectHandle<WasmDispatchTable> dispatch_table,
         DirectHandle<WasmTrustedInstanceData> instance, int table_index);
  // Internal helpers for management of the uses list. These could be factored
  // out into a class similar to WeakArrayList if there are additional use
  // cases for them.
  // The first slot in the list is the used length. After that, we store
  // pairs of <instance, table_index>.
  static Tagged<ProtectedWeakFixedArray> MaybeGrowUsesList(
      Isolate* isolate, DirectHandle<WasmDispatchTable> dispatch_table);

  static V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Handle<WasmDispatchTable> New(
      Isolate* isolate, int length, wasm::CanonicalValueType table_type);
  static V8_WARN_UNUSED_RESULT Handle<WasmDispatchTable> Grow(
      Isolate*, Handle<WasmDispatchTable>, uint32_t new_length);

  DECL_PRINTER(WasmDispatchTable)
  DECL_VERIFIER(WasmDispatchTable)
  OBJECT_CONSTRUCTORS(WasmDispatchTable, ExposedTrustedObject);
};

// A Wasm exception that has been thrown out of Wasm code.
class V8_EXPORT_PRIVATE WasmExceptionPackage : public JSObject {
 public:
  static DirectHandle<WasmExceptionPackage> New(
      Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag,
      int encoded_size);

  static DirectHandle<WasmExceptionPackage> New(
      Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag,
      DirectHandle<FixedArray> values);

  // The below getters return {undefined} in case the given exception package
  // does not carry the requested values (i.e. is of a different type).
  static Handle<Object> GetExceptionTag(
      Isolate* isolate, DirectHandle<WasmExceptionPackage> exception_package);
  static Handle<Object> GetExceptionValues(
      Isolate* isolate, DirectHandle<WasmExceptionPackage> exception_package);

  // Determines the size of the array holding all encoded exception values.
  static uint32_t GetEncodedSize(const wasm::WasmTagSig* tag);
  static uint32_t GetEncodedSize(const wasm::WasmTag* tag);

  // In-object fields.
  enum { kTagIndex, kValuesIndex, kInObjectFieldCount };
  static constexpr int kSize =
      kHeaderSize + (kTaggedSize * kInObjectFieldCount);

  DECL_PRINTER(WasmExceptionPackage)
  DECL_VERIFIER(WasmExceptionPackage)
  OBJECT_CONSTRUCTORS(WasmExceptionPackage, JSObject);
};

void V8_EXPORT_PRIVATE
EncodeI32ExceptionValue(DirectHandle<FixedArray> encoded_values,
                        uint32_t* encoded_index, uint32_t value);

void V8_EXPORT_PRIVATE
EncodeI64ExceptionValue(DirectHandle<FixedArray> encoded_values,
                        uint32_t* encoded_index, uint64_t value);

void V8_EXPORT_PRIVATE
DecodeI32ExceptionValue(DirectHandle<FixedArray> encoded_values,
                        uint32_t* encoded_index, uint32_t* value);

void V8_EXPORT_PRIVATE
DecodeI64ExceptionValue(DirectHandle<FixedArray> encoded_values,
                        uint32_t* encoded_index, uint64_t* value);

bool UseGenericWasmToJSWrapper(wasm::ImportCallKind kind,
                               const wasm::CanonicalSig* sig,
                               wasm::Suspend suspend);

// A Wasm function that is wrapped and exported to JavaScript.
// Representation of WebAssembly.Function JavaScript-level object.
class WasmExportedFunction : public JSFunction {
 public:
  V8_EXPORT_PRIVATE static bool IsWasmExportedFunction(Tagged<Object> object);

  V8_EXPORT_PRIVATE static Handle<WasmExportedFunction> New(
      Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance_data,
      DirectHandle<WasmFuncRef> func_ref,
      DirectHandle<WasmInternalFunction> internal_function, int arity,
      DirectHandle<Code> export_wrapper);

  // Return a null-terminated string with the debug name in the form
  // 'js-to-wasm:<sig>'.
  static std::unique_ptr<char[]> GetDebugName(const wasm::CanonicalSig* sig);

  OBJECT_CONSTRUCTORS(WasmExportedFunction, JSFunction);
};

// A Wasm function that was created by wrapping a JavaScript callable.
// Representation of WebAssembly.Function JavaScript-level object.
class WasmJSFunction : public JSFunction {
 public:
  static bool IsWasmJSFunction(Tagged<Object> object);

  static DirectHandle<WasmJSFunction> New(Isolate* isolate,
                                          const wasm::FunctionSig* sig,
                                          DirectHandle<JSReceiver> callable,
                                          wasm::Suspend suspend);

  OBJECT_CONSTRUCTORS(WasmJSFunction, JSFunction);
};

// An external function exposed to Wasm via the C/C++ API.
class WasmCapiFunction : public JSFunction {
 public:
  static bool IsWasmCapiFunction(Tagged<Object> object);

  static DirectHandle<WasmCapiFunction> New(Isolate* isolate,
                                            Address call_target,
                                            DirectHandle<Foreign> embedder_data,
                                            wasm::CanonicalTypeIndex sig_index,
                                            const wasm::CanonicalSig* sig);

  const wasm::CanonicalSig* sig() const;

  // Checks whether the given {sig} has the same parameter types as the
  // serialized signature stored within this C-API function object.
  bool MatchesSignature(
      wasm::CanonicalTypeIndex other_canonical_sig_index) const;

  OBJECT_CONSTRUCTORS(WasmCapiFunction, JSFunction);
};

// Any external function that can be imported/exported in modules. This abstract
// class just dispatches to the following concrete classes:
//  - {WasmExportedFunction}: A proper Wasm function exported from a module.
//  - {WasmJSFunction}: A function constructed via WebAssembly.Function in JS.
//  - {WasmCapiFunction}: A function constructed via the C/C++ API.
class WasmExternalFunction : public JSFunction {
 public:
  static bool IsWasmExternalFunction(Tagged<Object> object);

  inline Tagged<WasmFuncRef> func_ref() const;

  OBJECT_CONSTRUCTORS(WasmExternalFunction, JSFunction);
};

class WasmFunctionData
    : public TorqueGeneratedWasmFunctionData<WasmFunctionData,
                                             ExposedTrustedObject> {
 public:
  DECL_CODE_POINTER_ACCESSORS(wrapper_code)
  DECL_PROTECTED_POINTER_ACCESSORS(internal, WasmInternalFunction)

  DECL_PRINTER(WasmFunctionData)

  using BodyDescriptor = StackedBodyDescriptor<
      FixedExposedTrustedObjectBodyDescriptor<
          WasmFunctionData, kWasmFunctionDataIndirectPointerTag>,
      WithStrongCodePointer<kWrapperCodeOffset>,
      WithProtectedPointer<kProtectedInternalOffset>>;

  using SuspendField = base::BitField<wasm::Suspend, 0, 1>;
  using PromiseField = SuspendField::Next<wasm::Promise, 1>;

  TQ_OBJECT_CONSTRUCTORS(WasmFunctionData)
};

// Information for a WasmExportedFunction which is referenced as the function
// data of the SharedFunctionInfo underlying the function. For details please
// see the {SharedFunctionInfo::HasWasmExportedFunctionData} predicate.
class WasmExportedFunctionData
    : public TorqueGeneratedWasmExportedFunctionData<WasmExportedFunctionData,
                                                     WasmFunctionData> {
 public:
  DECL_PROTECTED_POINTER_ACCESSORS(instance_data, WasmTrustedInstanceData)
  DECL_CODE_POINTER_ACCESSORS(c_wrapper_code)

  DECL_PRIMITIVE_ACCESSORS(sig, const wasm::CanonicalSig*)
  // Prefer to use this convenience wrapper of the Torque-generated
  // {canonical_type_index()}.
  inline wasm::CanonicalTypeIndex sig_index() const;

  inline bool is_promising() const;

  bool MatchesSignature(wasm::CanonicalTypeIndex other_canonical_sig_index);

  // Dispatched behavior.
  DECL_PRINTER(WasmExportedFunctionData)
  DECL_VERIFIER(WasmExportedFunctionData)

  using BodyDescriptor = StackedBodyDescriptor<
      SubclassBodyDescriptor<WasmFunctionData::BodyDescriptor,
                             FixedBodyDescriptorFor<WasmExportedFunctionData>>,
      WithProtectedPointer<kProtectedInstanceDataOffset>,
      WithStrongCodePointer<kCWrapperCodeOffset>>;

  TQ_OBJECT_CONSTRUCTORS(WasmExportedFunctionData)
};

class WasmImportData
    : public TorqueGeneratedWasmImportData<WasmImportData, TrustedObject> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(WasmImportData)

  DECL_PROTECTED_POINTER_ACCESSORS(instance_data, WasmTrustedInstanceData)
  DECL_PROTECTED_POINTER_ACCESSORS(call_origin, TrustedObject)

  DECL_PRIMITIVE_ACCESSORS(suspend, wasm::Suspend)
  DECL_PRIMITIVE_ACCESSORS(table_slot, uint32_t)

  static constexpr int kInvalidCallOrigin = 0;

  void SetIndexInTableAsCallOrigin(Tagged<WasmDispatchTable> table,
                                   int entry_index);

  void SetFuncRefAsCallOrigin(Tagged<WasmInternalFunction> func);

  using BodyDescriptor =
      StackedBodyDescriptor<FixedBodyDescriptorFor<WasmImportData>,
                            WithProtectedPointer<kProtectedInstanceDataOffset>,
                            WithProtectedPointer<kProtectedCallOriginOffset>>;

  // Usage of the {bit_field()}.
  // "Suspend" is always present.
  using SuspendField = base::BitField<wasm::Suspend, 0, 1>;
  // "TableSlot" is populated when {protected_call_origin} is a
  // {WasmDispatchTable}, and describes the slot in that table.
  static constexpr int kTableSlotBits = 24;
  static_assert(wasm::kV8MaxWasmTableSize < (1u << kTableSlotBits));
  using TableSlotField = SuspendField::Next<uint32_t, kTableSlotBits>;

  TQ_OBJECT_CONSTRUCTORS(WasmImportData)
};

class WasmInternalFunction
    : public TorqueGeneratedWasmInternalFunction<WasmInternalFunction,
                                                 ExposedTrustedObject> {
 public:
  // Get the external function if it exists. Returns true and writes to the
  // output parameter if an external function exists. Returns false otherwise.
  bool try_get_external(Tagged<JSFunction>* result);

  V8_EXPORT_PRIVATE static Handle<JSFunction> GetOrCreateExternal(
      DirectHandle<WasmInternalFunction> internal);

  DECL_PROTECTED_POINTER_ACCESSORS(implicit_arg, TrustedObject)

  V8_INLINE WasmCodePointer call_target();
  V8_INLINE void set_call_target(WasmCodePointer code_pointer);

  // Dispatched behavior.
  DECL_PRINTER(WasmInternalFunction)

  using BodyDescriptor = StackedBodyDescriptor<
      FixedExposedTrustedObjectBodyDescriptor<
          WasmInternalFunction, kWasmInternalFunctionIndirectPointerTag>,
      WithProtectedPointer<kProtectedImplicitArgOffset>>;

  TQ_OBJECT_CONSTRUCTORS(WasmInternalFunction)
};

class WasmFuncRef : public TorqueGeneratedWasmFuncRef<WasmFuncRef, HeapObject> {
 public:
  DECL_TRUSTED_POINTER_ACCESSORS(internal, WasmInternalFunction)

  DECL_PRINTER(WasmFuncRef)

  using BodyDescriptor = StackedBodyDescriptor<
      FixedBodyDescriptorFor<WasmFuncRef>,
      WithStrongTrustedPointer<kTrustedInternalOffset,
                               kWasmInternalFunctionIndirectPointerTag>>;

  TQ_OBJECT_CONSTRUCTORS(WasmFuncRef)
};

// Information for a WasmJSFunction which is referenced as the function data of
// the SharedFunctionInfo underlying the function. For details please see the
// {SharedFunctionInfo::HasWasmJSFunctionData} predicate.
class WasmJSFunctionData
    : public TorqueGeneratedWasmJSFunctionData<WasmJSFunctionData,
                                               WasmFunctionData> {
 public:
  // The purpose of this class is to provide lifetime management for compiled
  // wrappers: the {WasmJSFunction} owns an {OffheapData} via {TrustedManaged},
  // which decrements the wrapper's refcount when the {WasmJSFunction} is
  // garbage-collected.
  class OffheapData {
   public:
    explicit OffheapData(uint64_t signature_hash)
        : signature_hash_(signature_hash) {}
    ~OffheapData();

    // These functions return the CPT entry owned by this class.
    WasmCodePointer set_compiled_wrapper(wasm::WasmCode* wrapper);
    WasmCodePointer set_generic_wrapper(Address call_target);

   private:
    WasmCodePointer wrapper_code_pointer_ = wasm::kInvalidWasmCodePointer;
    wasm::WasmCode* wrapper_{nullptr};
    const uint64_t signature_hash_;
  };

  DECL_PROTECTED_POINTER_ACCESSORS(protected_offheap_data,
                                   TrustedManaged<OffheapData>)
  inline OffheapData* offheap_data() const;

  Tagged<JSReceiver> GetCallable() const;
  wasm::Suspend GetSuspend() const;
  const wasm::CanonicalSig* GetSignature() const;
  // Prefer to use this convenience wrapper of the Torque-generated
  // {canonical_sig_index()}.
  inline wasm::CanonicalTypeIndex sig_index() const;
  bool MatchesSignature(
      wasm::CanonicalTypeIndex other_canonical_sig_index) const;

  // Dispatched behavior.
  DECL_PRINTER(WasmJSFunctionData)

  using BodyDescriptor = StackedBodyDescriptor<
      SubclassBodyDescriptor<WasmFunctionData::BodyDescriptor,
                             FixedBodyDescriptorFor<WasmJSFunctionData>>,
      WithProtectedPointer<kProtectedOffheapDataOffset>>;

 private:
  TQ_OBJECT_CONSTRUCTORS(WasmJSFunctionData)
};

class WasmCapiFunctionData
    : public TorqueGeneratedWasmCapiFunctionData<WasmCapiFunctionData,
                                                 WasmFunctionData> {
 public:
  // Prefer to use this convenience wrapper of the Torque-generated
  // {canonical_sig_index()}.
  inline wasm::CanonicalTypeIndex sig_index() const;

  DECL_PRINTER(WasmCapiFunctionData)

  using BodyDescriptor =
      SubclassBodyDescriptor<WasmFunctionData::BodyDescriptor,
                             FixedBodyDescriptorFor<WasmCapiFunctionData>>;

  TQ_OBJECT_CONSTRUCTORS(WasmCapiFunctionData)
};

class WasmResumeData
    : public TorqueGeneratedWasmResumeData<WasmResumeData, HeapObject> {
 public:
  using BodyDescriptor =
      FlexibleBodyDescriptor<WasmResumeData::kStartOfStrongFieldsOffset>;
  DECL_PRINTER(WasmResumeData)
  TQ_OBJECT_CONSTRUCTORS(WasmResumeData)
};

class WasmScript : public AllStatic {
 public:
  // Position used for storing "on entry" breakpoints (a.k.a. instrumentation
  // breakpoints). This would be an illegal position for any other breakpoint.
  static constexpr int kOnEntryBreakpointPosition = -1;

  // Set a breakpoint on the given byte position inside the given module.
  // This will affect all live and future instances of the module.
  // The passed position might be modified to point to the next breakable
  // location inside the same function.
  // If it points outside a function, or behind the last breakable location,
  // this function returns false and does not set any breakpoint.
  V8_EXPORT_PRIVATE static bool SetBreakPoint(
      DirectHandle<Script>, int* position,
      DirectHandle<BreakPoint> break_point);

  // Set an "on entry" breakpoint (a.k.a. instrumentation breakpoint) inside
  // the given module. This will affect all live and future instances of the
  // module.
  V8_EXPORT_PRIVATE static void SetInstrumentationBreakpoint(
      DirectHandle<Script>, DirectHandle<BreakPoint> break_point);

  // Set a breakpoint on first breakable position of the given function index
  // inside the given module. This will affect all live and future instances of
  // the module.
  V8_EXPORT_PRIVATE static bool SetBreakPointOnFirstBreakableForFunction(
      DirectHandle<Script>, int function_index,
      DirectHandle<BreakPoint> break_point);

  // Set a breakpoint at the breakable offset of the given function index
  // inside the given module. This will affect all live and future instances of
  // the module.
  V8_EXPORT_PRIVATE static bool SetBreakPointForFunction(
      DirectHandle<Script>, int function_index, int breakable_offset,
      DirectHandle<BreakPoint> break_point);

  // Remove a previously set breakpoint at the given byte position inside the
  // given module. If this breakpoint is not found this function returns false.
  V8_EXPORT_PRIVATE static bool ClearBreakPoint(
      DirectHandle<Script>, int position, DirectHandle<BreakPoint> break_point);

  // Remove a previously set breakpoint by id. If this breakpoint is not found,
  // returns false.
  V8_EXPORT_PRIVATE static bool ClearBreakPointById(DirectHandle<Script>,
                                                    int breakpoint_id);

  // Remove all set breakpoints.
  static void ClearAllBreakpoints(Tagged<Script>);

  // Get a list of all possible breakpoints within a given range of this module.
  V8_EXPORT_PRIVATE static bool GetPossibleBreakpoints(
      wasm::NativeModule* native_module, const debug::Location& start,
      const debug::Location& end, std::vector<debug::BreakLocation>* locations);

  // Return an empty handle if no breakpoint is hit at that location, or a
  // FixedArray with all hit breakpoint objects.
  static MaybeDirectHandle<FixedArray> CheckBreakPoints(
      Isolate*, DirectHandle<Script>, int position,
      StackFrameId stack_frame_id);

 private:
  // Helper functions that update the breakpoint info list.
  static void AddBreakpointToInfo(DirectHandle<Script>, int position,
                                  DirectHandle<BreakPoint> break_point);
};

// Tags provide an object identity for each exception defined in a wasm module
// header. They are referenced by the following fields:
//  - {WasmTagObject::tag}: The tag of the {Tag} object.
//  - {WasmInstanceObject::tags_table}: List of tags used by an instance.
class WasmExceptionTag
    : public TorqueGeneratedWasmExceptionTag<WasmExceptionTag, Struct> {
 public:
  V8_EXPORT_PRIVATE static Handle<WasmExceptionTag> New(Isolate* isolate,
                                                        int index);

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmExceptionTag)
};

// Data annotated to the asm.js Module function. Used for later instantiation of
// that function.
class AsmWasmData : public TorqueGeneratedAsmWasmData<AsmWasmData, Struct> {
 public:
  static Handle<AsmWasmData> New(
      Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
      DirectHandle<HeapNumber> uses_bitset);

  DECL_PRINTER(AsmWasmData)

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(AsmWasmData)
};

class WasmTypeInfo
    : public TorqueGeneratedWasmTypeInfo<WasmTypeInfo, HeapObject> {
 public:
  inline wasm::CanonicalTypeIndex type_index() const;
  inline wasm::CanonicalValueType element_type() const;  // Only for WasmArrays.

  DECL_PRINTER(WasmTypeInfo)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmTypeInfo)
};

class WasmObject : public TorqueGeneratedWasmObject<WasmObject, JSReceiver> {
 protected:
  // Returns boxed value of the object's field/element with given type and
  // offset.
  static inline DirectHandle<Object> ReadValueAt(Isolate* isolate,
                                                 DirectHandle<HeapObject> obj,
                                                 wasm::CanonicalValueType type,
                                                 uint32_t offset);

 private:
  template <typename ElementType>
  static ElementType FromNumber(Tagged<Object> value);

  TQ_OBJECT_CONSTRUCTORS(WasmObject)
};

class WasmStruct : public TorqueGeneratedWasmStruct<WasmStruct, WasmObject> {
 public:
  static const wasm::CanonicalStructType* GcSafeType(Tagged<Map> map);
  static inline int Size(const wasm::StructType* type);
  static inline int Size(const wasm::CanonicalStructType* type);
  static inline int GcSafeSize(Tagged<Map> map);
  static inline void EncodeInstanceSizeInMap(int instance_size,
                                             Tagged<Map> map);
  static inline int DecodeInstanceSizeFromMap(Tagged<Map> map);

  // Returns the address of the field at given offset.
  inline Address RawFieldAddress(int raw_offset);

  // Returns the ObjectSlot for tagged value at given offset.
  inline ObjectSlot RawField(int raw_offset);

  V8_EXPORT_PRIVATE wasm::WasmValue GetFieldValue(uint32_t field_index);

  static inline void SetField(Isolate* isolate, Handle<WasmStruct> obj,
                              uint32_t field_index, Handle<Object> value);

  DECL_PRINTER(WasmStruct)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmStruct)
};

int WasmStruct::Size(const wasm::StructType* type) {
  // Object size must fit into a Smi (because of filler objects), and its
  // computation must not overflow.
  static_assert(Smi::kMaxValue <= kMaxInt);
  DCHECK_LE(type->total_fields_size(), Smi::kMaxValue - kHeaderSize);
  return std::max(kHeaderSize + static_cast<int>(type->total_fields_size()),
                  Heap::kMinObjectSizeInTaggedWords * kTaggedSize);
}

int WasmStruct::Size(const wasm::CanonicalStructType* type) {
  // Object size must fit into a Smi (because of filler objects), and its
  // computation must not overflow.
  static_assert(Smi::kMaxValue <= kMaxInt);
  DCHECK_LE(type->total_fields_size(), Smi::kMaxValue - kHeaderSize);
  return std::max(kHeaderSize + static_cast<int>(type->total_fields_size()),
                  Heap::kMinObjectSizeInTaggedWords * kTaggedSize);
}

class WasmArray : public TorqueGeneratedWasmArray<WasmArray, WasmObject> {
 public:
  static inline wasm::CanonicalTypeIndex type_index(Tagged<Map> map);
  static inline const wasm::CanonicalValueType GcSafeElementType(
      Tagged<Map> map);

  // Get the {ObjectSlot} corresponding to the element at {index}. Requires that
  // this is a reference array.
  inline ObjectSlot ElementSlot(uint32_t index);
  V8_EXPORT_PRIVATE wasm::WasmValue GetElement(uint32_t index);

  static inline int SizeFor(Tagged<Map> map, int length);

  // Returns boxed value of the array's element.
  static inline DirectHandle<Object> GetElement(Isolate* isolate,
                                                DirectHandle<WasmArray> array,
                                                uint32_t index);

  void SetTaggedElement(uint32_t index, DirectHandle<Object> value,
                        WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Returns the offset/Address of the element at {index}.
  inline uint32_t element_offset(uint32_t index);
  inline Address ElementAddress(uint32_t index);

  static constexpr int MaxLength(uint32_t element_size_bytes) {
    // The total object size must fit into a Smi, for filler objects. To make
    // the behavior of Wasm programs independent from the Smi configuration,
    // we hard-code the smaller of the two supported ranges.
    return (SmiTagging<4>::kSmiMaxValue - kHeaderSize) / element_size_bytes;
  }

  static int MaxLength(const wasm::ArrayType* type) {
    return MaxLength(type->element_type().value_kind_size());
  }

  static inline void EncodeElementSizeInMap(int element_size, Tagged<Map> map);
  static inline int DecodeElementSizeFromMap(Tagged<Map> map);

  DECL_PRINTER(WasmArray)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmArray)
};

// A wasm delimited continuation.
class WasmContinuationObject
    : public TorqueGeneratedWasmContinuationObject<WasmContinuationObject,
                                                   HeapObject> {
 public:
  static DirectHandle<WasmContinuationObject> New(
      Isolate* isolate, wasm::StackMemory* stack,
      wasm::JumpBuffer::StackState state,
      AllocationType allocation_type = AllocationType::kYoung);
  static DirectHandle<WasmContinuationObject> New(
      Isolate* isolate, wasm::StackMemory* stack,
      wasm::JumpBuffer::StackState state, DirectHandle<HeapObject> parent,
      AllocationType allocation_type = AllocationType::kYoung);

  DECL_EXTERNAL_POINTER_ACCESSORS(jmpbuf, Address)
  DECL_EXTERNAL_POINTER_ACCESSORS(stack, Address)

  DECL_PRINTER(WasmContinuationObject)

  using BodyDescriptor = StackedBodyDescriptor<
      FixedBodyDescriptorFor<WasmContinuationObject>,
      WithExternalPointer<kStackOffset, kWasmStackMemoryTag>,
      WithExternalPointer<kJmpbufOffset, kWasmContinuationJmpbufTag>>;

 private:
  TQ_OBJECT_CONSTRUCTORS(WasmContinuationObject)
};

// The suspender object provides an API to suspend and resume wasm code using
// promises. See: https://github.com/WebAssembly/js-promise-integration.
class WasmSuspenderObject
    : public TorqueGeneratedWasmSuspenderObject<WasmSuspenderObject,
                                                HeapObject> {
 public:
  using BodyDescriptor = FixedBodyDescriptorFor<WasmSuspenderObject>;
  enum State : int { kInactive = 0, kActive, kSuspended };
  DECL_PRINTER(WasmSuspenderObject)
  TQ_OBJECT_CONSTRUCTORS(WasmSuspenderObject)
};

class WasmSuspendingObject
    : public TorqueGeneratedWasmSuspendingObject<WasmSuspendingObject,
                                                 JSObject> {
 public:
  V8_EXPORT_PRIVATE static DirectHandle<WasmSuspendingObject> New(
      Isolate* isolate, DirectHandle<JSReceiver> callable);
  DECL_PRINTER(WasmSuspendingObject)
  TQ_OBJECT_CONSTRUCTORS(WasmSuspendingObject)
};

class WasmNull : public TorqueGeneratedWasmNull<WasmNull, HeapObject> {
 public:
#if V8_STATIC_ROOTS_BOOL || V8_STATIC_ROOTS_GENERATION_BOOL
  // TODO(manoskouk): Make it smaller if able and needed.
  static constexpr int kSize = 64 * KB + kTaggedSize;
  // Payload should be a multiple of page size.
  static_assert((kSize - kTaggedSize) % kMinimumOSPageSize == 0);
  // Any wasm struct offset should fit in the object.
  static_assert(kSize >=
                WasmStruct::kHeaderSize +
                    (wasm::kMaxStructFieldIndexForImplicitNullCheck + 1) *
                        kSimd128Size);

  Address payload() { return ptr() + kHeaderSize - kHeapObjectTag; }
  static constexpr size_t kPayloadSize = kSize - kTaggedSize;
#else
  static constexpr int kSize = kTaggedSize;
#endif

  // WasmNull cannot use `FixedBodyDescriptorFor()` as its map is variable size
  // (not fixed size) as kSize is too large for a fixed-size map.
  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmNull)
};

#undef DECL_OPTIONAL_ACCESSORS

DirectHandle<Map> CreateFuncRefMap(Isolate* isolate,
                                   wasm::CanonicalTypeIndex type,
                                   Handle<Map> opt_rtt_parent);

namespace wasm {
// Takes a {value} in the JS representation and typechecks it according to
// {expected}. If the typecheck succeeds, returns the wasm representation of the
// object; otherwise, returns the empty handle.
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, Handle<Object> value,
                                   CanonicalValueType expected,
                                   const char** error_message);

// Utility which canonicalizes {expected} in addition.
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, const WasmModule* module,
                                   Handle<Object> value, ValueType expected,
                                   const char** error_message);

// Takes a {value} in the Wasm representation and transforms it to the
// respective JS representation. The caller is responsible for not providing an
// object which cannot be transformed to JS.
Handle<Object> WasmToJSObject(Isolate* isolate, Handle<Object> value);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_WASM_WASM_OBJECTS_H_

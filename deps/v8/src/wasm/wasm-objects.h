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

enum class WasmTableFlag : uint8_t { kTable32, kTable64 };

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
  Handle<TrustedObject> implicit_arg() { return implicit_arg_; }
  Address call_target() { return call_target_; }

#if V8_ENABLE_DRUMBRAKE
  int target_func_index() { return target_func_index_; }
#endif  // V8_ENABLE_DRUMBRAKE

 private:
  Handle<TrustedObject> implicit_arg_;
  Address call_target_;

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
  inline ImportedFunctionEntry(Isolate*, DirectHandle<WasmInstanceObject>,
                               int index);
  inline ImportedFunctionEntry(Handle<WasmTrustedInstanceData>, int index);

  // Initialize this entry as a Wasm to JS call. This accepts the isolate as a
  // parameter since it allocates a WasmImportData.
  void SetGenericWasmToJs(Isolate*, DirectHandle<JSReceiver> callable,
                          wasm::Suspend suspend, const wasm::FunctionSig* sig);
  V8_EXPORT_PRIVATE void SetCompiledWasmToJs(
      Isolate*, DirectHandle<JSReceiver> callable,
      const wasm::WasmCode* wasm_to_js_wrapper, wasm::Suspend suspend,
      const wasm::FunctionSig* sig);

  // Initialize this entry as a Wasm to Wasm call.
  void SetWasmToWasm(Tagged<WasmTrustedInstanceData> target_instance_object,
                     Address call_target
#if V8_ENABLE_DRUMBRAKE
                     ,
                     int exported_function_index
#endif  // V8_ENABLE_DRUMBRAKE
  );

  Tagged<JSReceiver> callable();
  Tagged<Object> maybe_callable();
  Tagged<Object> implicit_arg();
  Address target();
  void set_target(Address new_target);

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
  static MaybeHandle<String> GetModuleNameOrNull(
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
// This should be checked before writing an untrusted function reference
// into a dispatch table (e.g. via WasmTableObject::Set).
bool FunctionSigMatchesTable(uint32_t canonical_sig_id,
                             const wasm::WasmModule* module, int table_index);
#endif

// Representation of a WebAssembly.Table JavaScript-level object.
class WasmTableObject
    : public TorqueGeneratedWasmTableObject<WasmTableObject, JSObject> {
 public:
  class BodyDescriptor;

  inline wasm::ValueType type();

  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)

  V8_EXPORT_PRIVATE static int Grow(Isolate* isolate,
                                    DirectHandle<WasmTableObject> table,
                                    uint32_t count,
                                    DirectHandle<Object> init_value);

  V8_EXPORT_PRIVATE static Handle<WasmTableObject> New(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_data,
      wasm::ValueType type, uint32_t initial, bool has_maximum,
      uint32_t maximum, DirectHandle<Object> initial_value,
      WasmTableFlag table_type = WasmTableFlag::kTable32);

  // Store that a specific instance uses this table, in order to update the
  // instance's dispatch table when this table grows (and hence needs to
  // allocate a new dispatch table).
  V8_EXPORT_PRIVATE static void AddUse(
      Isolate* isolate, DirectHandle<WasmTableObject> table,
      Handle<WasmInstanceObject> instance_object, int table_index);

  bool is_in_bounds(uint32_t entry_index);

  // Overwrite the Torque-generated method that returns an int.
  inline bool is_table64() const;

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
  static void UpdateDispatchTables(
      Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
      const wasm::WasmFunction* func,
      DirectHandle<WasmTrustedInstanceData> target_instance
#if V8_ENABLE_DRUMBRAKE
      ,
      int target_func_index
#endif  // V8_ENABLE_DRUMBRAKE
  );
  static void UpdateDispatchTables(Isolate* isolate,
                                   DirectHandle<WasmTableObject> table,
                                   int entry_index,
                                   DirectHandle<WasmJSFunction> function);
  static void UpdateDispatchTables(
      Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
      DirectHandle<WasmCapiFunction> capi_function);

  void ClearDispatchTables(int index);

  V8_EXPORT_PRIVATE static void SetFunctionTablePlaceholder(
      Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      int func_index);

  // This function reads the content of a function table entry and returns it
  // through the output parameters.
  static void GetFunctionTableEntry(
      Isolate* isolate, const wasm::WasmModule* module,
      DirectHandle<WasmTableObject> table, int entry_index, bool* is_valid,
      bool* is_null, MaybeHandle<WasmTrustedInstanceData>* instance_data,
      int* function_index,
      MaybeDirectHandle<WasmJSFunction>* maybe_js_function);

 private:
  // {entry} is either {Null} or a {WasmInternalFunction}.
  static void SetFunctionTableEntry(Isolate* isolate,
                                    DirectHandle<WasmTableObject> table,
                                    int entry_index,
                                    DirectHandle<Object> entry);

  TQ_OBJECT_CONSTRUCTORS(WasmTableObject)
};

// Representation of a WebAssembly.Memory JavaScript-level object.
class WasmMemoryObject
    : public TorqueGeneratedWasmMemoryObject<WasmMemoryObject, JSObject> {
 public:
  DECL_ACCESSORS(instances, Tagged<WeakArrayList>)

  // Add a use of this memory object to the given instance. This updates the
  // internal weak list of instances that use this memory and also updates the
  // fields of the instance to reference this memory's buffer.
  // Note that we update both the non-shared and shared (if any) parts of the
  // instance for faster access to shared memory.
  V8_EXPORT_PRIVATE static void UseInInstance(
      Isolate* isolate, DirectHandle<WasmMemoryObject> memory,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      Handle<WasmTrustedInstanceData> shared_trusted_instance_data,
      int memory_index_in_instance);
  inline bool has_maximum_pages();

  // Overwrite the Torque-generated method that returns an int.
  inline bool is_memory64() const;

  V8_EXPORT_PRIVATE static Handle<WasmMemoryObject> New(
      Isolate* isolate, Handle<JSArrayBuffer> buffer, int maximum,
      WasmMemoryFlag memory_type);

  V8_EXPORT_PRIVATE static MaybeHandle<WasmMemoryObject> New(
      Isolate* isolate, int initial, int maximum, SharedFlag shared,
      WasmMemoryFlag memory_type);

  // Assign a new (grown) buffer to this memory, also updating the shortcut
  // fields of all instances that use this memory.
  void SetNewBuffer(Tagged<JSArrayBuffer> new_buffer);

  V8_EXPORT_PRIVATE static int32_t Grow(Isolate*, Handle<WasmMemoryObject>,
                                        uint32_t pages);

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
      Isolate* isolate, Handle<WasmTrustedInstanceData> instance_object,
      MaybeHandle<JSArrayBuffer> maybe_untagged_buffer,
      MaybeHandle<FixedArray> maybe_tagged_buffer, wasm::ValueType type,
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

  static void EnsureMinimumDispatchTableSize(
      Isolate* isolate,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      int table_index, int minimum_size);

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

  Address GetCallTarget(uint32_t func_index);

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

  // Imports a constructed {WasmJSFunction} into the indirect function table of
  // this instance. Note that this might trigger wrapper compilation, since a
  // {WasmJSFunction} is instance-independent and just wraps a JS callable.
  static void ImportWasmJSFunctionIntoTable(
      Isolate* isolate,
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      int table_index, int entry_index,
      DirectHandle<WasmJSFunction> js_function);

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
  bool MatchesSignature(uint32_t expected_canonical_type_index);

  static Handle<WasmTagObject> New(
      Isolate* isolate, const wasm::FunctionSig* sig,
      uint32_t canonical_type_index, DirectHandle<HeapObject> tag,
      DirectHandle<WasmTrustedInstanceData> instance);

  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)

  TQ_OBJECT_CONSTRUCTORS(WasmTagObject)
};

// The dispatch table is referenced from a WasmTableObject and from every
// WasmTrustedInstanceData which uses the table. It is used from generated code
// for executing indirect calls.
class WasmDispatchTable : public TrustedObject {
 public:
#if V8_ENABLE_DRUMBRAKE
  static const uint32_t kInvalidFunctionIndex = UINT_MAX;
#endif  // V8_ENABLE_DRUMBRAKE
  class BodyDescriptor;

  static constexpr size_t kLengthOffset = kHeaderSize;
  static constexpr size_t kCapacityOffset = kLengthOffset + kUInt32Size;
  static constexpr size_t kEntriesOffset = kCapacityOffset + kUInt32Size;

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

  // Accessors.
  // {implicit_arg} will be a WasmImportData, a WasmTrustedInstanceData, or
  // Smi::zero() (if the entry was cleared).
  inline Tagged<Object> implicit_arg(int index) const;
  inline Address target(int index) const;
  inline int sig(int index) const;

  // Set an entry for indirect calls.
  // {implicit_arg} has to be a WasmImportData, a WasmTrustedInstanceData, or
  // Smi::zero().
  void V8_EXPORT_PRIVATE Set(int index, Tagged<Object> implicit_arg,
                             Address call_target, int sig_id
#if V8_ENABLE_DRUMBRAKE
                             ,
                             uint32_t function_index
#endif  // V8_ENABLE_DRUMBRAKE
  );
#if V8_ENABLE_DRUMBRAKE
  inline uint32_t function_index(int index) const;
#endif  // V8_ENABLE_DRUMBRAKE

  // Set an entry for an import. We check signatures statically there, so the
  // signature is not updated in the dispatch table.
  // {implicit_arg} has to be a WasmImportData or a WasmTrustedInstanceData.
  void V8_EXPORT_PRIVATE SetForImport(int index,
                                      Tagged<TrustedObject> implicit_arg,
                                      Address call_target);

  void Clear(int index);
  void SetTarget(int index, Address call_target);

  static V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Handle<WasmDispatchTable> New(
      Isolate* isolate, int length);
  static V8_WARN_UNUSED_RESULT Handle<WasmDispatchTable> Grow(
      Isolate*, Handle<WasmDispatchTable>, int new_length);

  DECL_PRINTER(WasmDispatchTable)
  DECL_VERIFIER(WasmDispatchTable)
  OBJECT_CONSTRUCTORS(WasmDispatchTable, TrustedObject);
};

// A Wasm exception that has been thrown out of Wasm code.
class V8_EXPORT_PRIVATE WasmExceptionPackage : public JSObject {
 public:
  static Handle<WasmExceptionPackage> New(
      Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag,
      int encoded_size);

  static Handle<WasmExceptionPackage> New(
      Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag,
      DirectHandle<FixedArray> values);

  // The below getters return {undefined} in case the given exception package
  // does not carry the requested values (i.e. is of a different type).
  static Handle<Object> GetExceptionTag(
      Isolate* isolate, Handle<WasmExceptionPackage> exception_package);
  static Handle<Object> GetExceptionValues(
      Isolate* isolate, Handle<WasmExceptionPackage> exception_package);

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
                               const wasm::FunctionSig* sig,
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
  static std::unique_ptr<char[]> GetDebugName(const wasm::FunctionSig* sig);

  OBJECT_CONSTRUCTORS(WasmExportedFunction, JSFunction);
};

// A Wasm function that was created by wrapping a JavaScript callable.
// Representation of WebAssembly.Function JavaScript-level object.
class WasmJSFunction : public JSFunction {
 public:
  static bool IsWasmJSFunction(Tagged<Object> object);

  static Handle<WasmJSFunction> New(Isolate* isolate,
                                    const wasm::FunctionSig* sig,
                                    Handle<JSReceiver> callable,
                                    wasm::Suspend suspend);

  OBJECT_CONSTRUCTORS(WasmJSFunction, JSFunction);
};

// An external function exposed to Wasm via the C/C++ API.
class WasmCapiFunction : public JSFunction {
 public:
  static bool IsWasmCapiFunction(Tagged<Object> object);

  static Handle<WasmCapiFunction> New(
      Isolate* isolate, Address call_target,
      DirectHandle<Foreign> embedder_data,
      DirectHandle<PodArray<wasm::ValueType>> serialized_signature,
      uintptr_t signature_hash);

  Tagged<PodArray<wasm::ValueType>> GetSerializedSignature() const;
  // Checks whether the given {sig} has the same parameter types as the
  // serialized signature stored within this C-API function object.
  bool MatchesSignature(uint32_t other_canonical_sig_index) const;
  const wasm::FunctionSig* GetSignature(Zone* zone) const;

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

  using SuspendField = base::BitField<wasm::Suspend, 0, 2>;
  using PromiseField = base::BitField<wasm::Promise, 2, 2>;

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

  DECL_PRIMITIVE_ACCESSORS(sig, const wasm::FunctionSig*)

  bool MatchesSignature(uint32_t other_canonical_sig_index);

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

  DECL_CODE_POINTER_ACCESSORS(code)

  DECL_PROTECTED_POINTER_ACCESSORS(instance_data, WasmTrustedInstanceData)

  static constexpr int kInvalidCallOrigin = 0;

  static void SetImportIndexAsCallOrigin(DirectHandle<WasmImportData> ref,
                                         int entry_index);

  static bool CallOriginIsImportIndex(DirectHandle<Object> call_origin);

  static bool CallOriginIsIndexInTable(DirectHandle<Object> call_origin);

  static int CallOriginAsIndex(DirectHandle<Object> call_origin);

  static void SetIndexInTableAsCallOrigin(DirectHandle<WasmImportData> ref,
                                          int entry_index);

  static void SetCrossInstanceTableIndexAsCallOrigin(
      Isolate* isolate, DirectHandle<WasmImportData> ref,
      DirectHandle<WasmInstanceObject> instance_object, int entry_index);

  static void SetFuncRefAsCallOrigin(DirectHandle<WasmImportData> ref,
                                     DirectHandle<WasmFuncRef> func_ref);

  using BodyDescriptor =
      StackedBodyDescriptor<FixedBodyDescriptorFor<WasmImportData>,
                            WithProtectedPointer<kProtectedInstanceDataOffset>,
                            WithStrongCodePointer<kCodeOffset>>;

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
  Tagged<JSReceiver> GetCallable() const;
  wasm::Suspend GetSuspend() const;
  const wasm::FunctionSig* GetSignature() const;
  bool MatchesSignature(uint32_t other_canonical_sig_index) const;

  // Dispatched behavior.
  DECL_PRINTER(WasmJSFunctionData)

  using BodyDescriptor =
      SubclassBodyDescriptor<WasmFunctionData::BodyDescriptor,
                             FixedBodyDescriptorFor<WasmJSFunctionData>>;

 private:
  TQ_OBJECT_CONSTRUCTORS(WasmJSFunctionData)
};

class WasmCapiFunctionData
    : public TorqueGeneratedWasmCapiFunctionData<WasmCapiFunctionData,
                                                 WasmFunctionData> {
 public:
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
  static MaybeHandle<FixedArray> CheckBreakPoints(Isolate*,
                                                  DirectHandle<Script>,
                                                  int position,
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
  DECL_EXTERNAL_POINTER_ACCESSORS(native_type, Address)
  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)

  DECL_PRINTER(WasmTypeInfo)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmTypeInfo)
};

class WasmObject : public TorqueGeneratedWasmObject<WasmObject, JSReceiver> {
 protected:
  // Returns boxed value of the object's field/element with given type and
  // offset.
  static inline Handle<Object> ReadValueAt(Isolate* isolate,
                                           DirectHandle<HeapObject> obj,
                                           wasm::ValueType type,
                                           uint32_t offset);

 private:
  template <typename ElementType>
  static ElementType FromNumber(Tagged<Object> value);

  TQ_OBJECT_CONSTRUCTORS(WasmObject)
};

class WasmStruct : public TorqueGeneratedWasmStruct<WasmStruct, WasmObject> {
 public:
  static inline wasm::StructType* type(Tagged<Map> map);
  inline wasm::StructType* type() const;
  static inline wasm::StructType* GcSafeType(Tagged<Map> map);
  static inline int Size(const wasm::StructType* type);
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

class WasmArray : public TorqueGeneratedWasmArray<WasmArray, WasmObject> {
 public:
  static inline wasm::ArrayType* type(Tagged<Map> map);
  inline wasm::ArrayType* type() const;
  static inline wasm::ArrayType* GcSafeType(Tagged<Map> map);

  // Get the {ObjectSlot} corresponding to the element at {index}. Requires that
  // this is a reference array.
  inline ObjectSlot ElementSlot(uint32_t index);
  V8_EXPORT_PRIVATE wasm::WasmValue GetElement(uint32_t index);

  static inline int SizeFor(Tagged<Map> map, int length);

  // Returns boxed value of the array's element.
  static inline Handle<Object> GetElement(Isolate* isolate,
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
  static Handle<WasmContinuationObject> New(
      Isolate* isolate, wasm::StackMemory* stack,
      wasm::JumpBuffer::StackState state,
      AllocationType allocation_type = AllocationType::kYoung);
  static Handle<WasmContinuationObject> New(
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
  V8_EXPORT_PRIVATE static Handle<WasmSuspendingObject> New(
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

Handle<Map> CreateFuncRefMap(Isolate* isolate, Handle<Map> opt_rtt_parent);

namespace wasm {
// Takes a {value} in the JS representation and typechecks it according to
// {expected}. If the typecheck succeeds, returns the wasm representation of the
// object; otherwise, returns the empty handle.
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, Handle<Object> value,
                                   ValueType expected, uint32_t canonical_index,
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

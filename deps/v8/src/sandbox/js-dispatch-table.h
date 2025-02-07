// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_JS_DISPATCH_TABLE_H_
#define V8_SANDBOX_JS_DISPATCH_TABLE_H_

#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/sandbox/external-entity-table.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

class Isolate;
class Counters;
class Code;

/**
 * The entries of a JSDispatchTable.
 *
 * An entry contains all information to call a JavaScript function in a
 * sandbox-compatible way: the entrypoint and the parameter count (~= the
 * signature of the function). The entrypoint will always point to the current
 * code of the function, thereby enabling seamless tiering.
 */
struct JSDispatchEntry {
  // We write-protect the JSDispatchTable on platforms that support it for
  // forward-edge CFI.
  static constexpr bool IsWriteProtected = true;

  inline void MakeJSDispatchEntry(Address object, Address entrypoint,
                                  uint16_t parameter_count, bool mark_as_alive);

  inline Address GetEntrypoint() const;
  inline Address GetCodePointer() const;
  inline uint16_t GetParameterCount() const;

  inline void SetCodeAndEntrypointPointer(Address new_object,
                                          Address new_entrypoint);

  // Make this entry a freelist entry, containing the index of the next entry
  // on the freelist.
  inline void MakeFreelistEntry(uint32_t next_entry_index);

  // Returns true if this entry is a freelist entry.
  inline bool IsFreelistEntry() const;

  // Get the index of the next entry on the freelist. This method may be
  // called even when the entry is not a freelist entry. However, the result
  // is only valid if this is a freelist entry. This behaviour is required
  // for efficient entry allocation, see TryAllocateEntryFromFreelist.
  inline uint32_t GetNextFreelistEntryIndex() const;

  // Mark this entry as alive during garbage collection.
  inline void Mark();

  // Unmark this entry during sweeping.
  inline void Unmark();

  // Test whether this entry is currently marked as alive.
  inline bool IsMarked() const;

  // Constants for access from generated code.
  // These are static_assert'ed to be correct in CheckFieldOffsets().
  static constexpr uintptr_t kEntrypointOffset = 0;
  static constexpr uintptr_t kCodeObjectOffset = 8;
  static constexpr uint32_t kObjectPointerShift = 16;
  static constexpr uint32_t kParameterCountMask = 0xffff;
  static void CheckFieldOffsets();

 private:
  friend class JSDispatchTable;

  // Freelist entries contain the index of the next free entry in their lower 32
  // bits and are tagged with this tag.
  static constexpr Address kFreeEntryTag = 0xffff000000000000ull;

  // The first word contains the pointer to the (executable) entrypoint.
  std::atomic<Address> entrypoint_;

  // The second word of the entry contains (1) the pointer to the code object
  // associated with this entry, (2) the marking bit of the entry in the LSB of
  // the object pointer (which must be unused as the address must be aligned),
  // and (3) the 16-bit parameter count. The parameter count is stored in the
  // lower 16 bits and therefore the pointer is shifted to the left. The final
  // format therefore looks as follows:
  //
  // +------------------------+-------------+-----------------+
  // |     Bits 63 ... 17     |   Bit 16    |  Bits 15 ... 0  |
  // |   HeapObject pointer   | Marking bit | Parameter count |
  // +------------------------+-------------+-----------------+
  //
  static constexpr Address kMarkingBit = 1 << 16;
  std::atomic<Address> encoded_word_;
};

static_assert(sizeof(JSDispatchEntry) == kJSDispatchTableEntrySize);

/**
 * JSDispatchTable.
 *
 * The JSDispatchTable achieves two central goals:
 *
 * 1. It provides fine-grained forward-edge CFI for JavaScript function calls.
 * Both in the context of the V8 Sandbox and for process-wide CFI. For the
 * sandbox, this requires keeping the table outside of the sandbox and storing
 * both the function's entrypoints and its parameter count in it. That way, it
 * is guaranteed that every JSFunction call (1) lands at a valid JavaScript
 * entrypoint, and (2) uses the correct signature (~= parameter count). For
 * process-wide CFI, this table is write-protected using for example Intel
 * PKEYs. That way, even an attacker with an arbitrary, process-wide write
 * primitive cannot execute arbitrary code via JavaScript functions.
 *
 * 2. It enables cheap and fast tiering. When the JSDispatchTable is used, a
 * group of related JSFunctions (roughly those sharing the same SFI) share one
 * table entry. When the functions should tier up or down, only the entry needs
 * to be updated to point to the new code. Without such a table, every function
 * entrypoint would need to check if it needs to tier up or down, thereby
 * incurring some overhead on every function invocation.
 */
class V8_EXPORT_PRIVATE JSDispatchTable
    : public ExternalEntityTable<JSDispatchEntry,
                                 kJSDispatchTableReservationSize> {
  using Base =
      ExternalEntityTable<JSDispatchEntry, kJSDispatchTableReservationSize>;

 public:
  // Size of a JSDispatchTable, for layout computation in IsolateData.
  static constexpr int kSize = 2 * kSystemPointerSize;

  static_assert(kMaxJSDispatchEntries == kMaxCapacity);
  static_assert(!kSupportsCompaction);

  JSDispatchTable() = default;
  JSDispatchTable(const JSDispatchTable&) = delete;
  JSDispatchTable& operator=(const JSDispatchTable&) = delete;

  // The Spaces used by a JSDispatchTable.
  using Space = Base::SpaceWithBlackAllocationSupport;

  // Retrieves the entrypoint of the entry referenced by the given handle.
  inline Address GetEntrypoint(JSDispatchHandle handle);

  // Retrieves the Code stored in the entry referenced by the given handle.
  //
  // TODO(saelo): in the future, we might store either a Code or a
  // BytecodeArray in the entries. At that point, this could be changed to
  // return a Tagged<Union<Code, BytecodeArray>>.
  inline Tagged<Code> GetCode(JSDispatchHandle handle);

  // Returns the address of the Code object stored in the specified entry.
  inline Address GetCodeAddress(JSDispatchHandle handle);

  // Retrieves the parameter count of the entry referenced by the given handle.
  inline uint16_t GetParameterCount(JSDispatchHandle handle);

  // Updates the entry referenced by the given handle to the given Code and its
  // entrypoint. The code must be compatible with the specified entry. In
  // particular, the two must use the same parameter count.
  inline void SetCode(JSDispatchHandle handle, Tagged<Code> new_code);

  // Allocates a new entry in the table and initialize it.
  //
  // This method is atomic and can be called from background threads.
  inline JSDispatchHandle AllocateAndInitializeEntry(Space* space,
                                                     uint16_t parameter_count);
  inline JSDispatchHandle AllocateAndInitializeEntry(Space* space,
                                                     uint16_t parameter_count,
                                                     Tagged<Code> code);

  // The following methods are used to pre allocate entries and then initialize
  // them later.
  JSDispatchHandle PreAllocateEntries(Space* space, int num,
                                      bool ensure_static_handles);
  bool PreAllocatedEntryNeedsInitialization(Space* space,
                                            JSDispatchHandle handle);
  void InitializePreAllocatedEntry(Space* space, JSDispatchHandle handle,
                                   Tagged<Code> code, uint16_t parameter_count);

  // Can be used to statically predict the handles if the pre allocated entries
  // are in the overall first read only segment of the whole table.
  static JSDispatchHandle GetStaticHandleForReadOnlySegmentEntry(int index) {
    return static_cast<JSDispatchHandle>(kInternalNullEntryIndex + 1 + index)
           << kJSDispatchHandleShift;
  }
  static bool InReadOnlySegment(JSDispatchHandle handle) {
    return HandleToIndex(handle) <= kEndOfInternalReadOnlySegment;
  }

  // Marks the specified entry as alive.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(JSDispatchHandle handle);

  // Frees all unmarked entries in the given space.
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while a space is being swept.
  //
  // Returns the number of live entries after sweeping.
  uint32_t Sweep(Space* space, Counters* counters);

  // Iterate over all active entries in the given space.
  //
  // The callback function will be invoked once for every entry that is
  // currently in use, i.e. has been allocated and not yet freed, and will
  // receive the handle of that entry.
  template <typename Callback>
  void IterateActiveEntriesIn(Space* space, Callback callback);

  template <typename Callback>
  void IterateMarkedEntriesIn(Space* space, Callback callback);

  // The base address of this table, for use in JIT compilers.
  Address base_address() const { return base(); }

  static JSDispatchTable* instance() {
    CheckInitialization(false);
    return instance_nocheck();
  }
  static void Initialize() {
    CheckInitialization(true);
    instance_nocheck()->Base::Initialize();
  }

#ifdef DEBUG
  inline void VerifyEntry(JSDispatchHandle handle, Space* space,
                          Space* ro_space);
#endif  // DEBUG

 private:
#ifdef DEBUG
  static std::atomic<bool> initialized_;
#endif  // DEBUG

  static void CheckInitialization(bool is_initializing) {
#ifdef DEBUG
    DCHECK_NE(is_initializing, initialized_.load());
    initialized_.store(true);
#endif  // DEBUG
  }

  static base::LeakyObject<JSDispatchTable> instance_;
  static JSDispatchTable* instance_nocheck() { return instance_.get(); }

  static uint32_t HandleToIndex(JSDispatchHandle handle) {
    uint32_t index = handle >> kJSDispatchHandleShift;
    DCHECK_EQ(handle, index << kJSDispatchHandleShift);
    return index;
  }
  static JSDispatchHandle IndexToHandle(uint32_t index) {
    JSDispatchHandle handle = index << kJSDispatchHandleShift;
    DCHECK_EQ(index, handle >> kJSDispatchHandleShift);
    return handle;
  }
};

static_assert(sizeof(JSDispatchTable) == JSDispatchTable::kSize);

// TODO(olivf): Remove this accessor and also unify implementation with
// GetProcessWideCodePointerTable().
V8_EXPORT_PRIVATE inline JSDispatchTable* GetProcessWideJSDispatchTable() {
  return JSDispatchTable::instance();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_JS_DISPATCH_TABLE_H_

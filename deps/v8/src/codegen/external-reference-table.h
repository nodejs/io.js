// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_EXTERNAL_REFERENCE_TABLE_H_
#define V8_CODEGEN_EXTERNAL_REFERENCE_TABLE_H_

#include "include/v8-memory-span.h"
#include "src/builtins/accessors.h"
#include "src/builtins/builtins.h"
#include "src/codegen/external-reference.h"
#include "src/logging/counters-definitions.h"

namespace v8 {
namespace internal {

class Isolate;

// ExternalReferenceTable is a helper class that defines the relationship
// between external references and their encodings. It is used to build
// hashmaps in ExternalReferenceEncoder and ExternalReferenceDecoder.
class ExternalReferenceTable {
 public:
  // For the nullptr ref, see the constructor.
  static constexpr int kSpecialReferenceCount = 1;
  static constexpr int kExternalReferenceCountIsolateIndependent =
      ExternalReference::kExternalReferenceCountIsolateIndependent;
  static constexpr int kExternalReferenceCountIsolateDependent =
      ExternalReference::kExternalReferenceCountIsolateDependent;
  static constexpr int kBuiltinsReferenceCount =
#define COUNT_C_BUILTIN(...) +1
      BUILTIN_LIST_C(COUNT_C_BUILTIN);
#undef COUNT_C_BUILTIN
  static constexpr int kRuntimeReferenceCount =
      Runtime::kNumFunctions -
      Runtime::kNumInlineFunctions;  // Don't count dupe kInline... functions.
  static constexpr int kIsolateAddressReferenceCount = kIsolateAddressCount;
  static constexpr int kAccessorReferenceCount =
      Accessors::kAccessorInfoCount + Accessors::kAccessorGetterCount +
      Accessors::kAccessorSetterCount + Accessors::kAccessorCallbackCount;
  // The number of stub cache external references, see AddStubCache.
  static constexpr int kStubCacheReferenceCount = 6 * 3;  // 3 stub caches
  static constexpr int kStatsCountersReferenceCount =
#define SC(...) +1
      STATS_COUNTER_NATIVE_CODE_LIST(SC);
#undef SC
  static constexpr int kSizeIsolateIndependent =
      kSpecialReferenceCount + kExternalReferenceCountIsolateIndependent +
      kBuiltinsReferenceCount + kRuntimeReferenceCount +
      kAccessorReferenceCount;
  static constexpr int kSize =
      kSizeIsolateIndependent + kExternalReferenceCountIsolateDependent +
      kIsolateAddressReferenceCount + kStubCacheReferenceCount +
      kStatsCountersReferenceCount;
  static constexpr uint32_t kEntrySize =
      static_cast<uint32_t>(kSystemPointerSize);
  static constexpr uint32_t kSizeInBytes = kSize * kEntrySize + 2 * kUInt32Size;

  Address address(uint32_t i) const { return ref_addr_[i]; }
  const char* name(uint32_t i) const { return ref_name_[i]; }

  bool is_initialized() const { return is_initialized_ == kInitialized; }

  static const char* ResolveSymbol(void* address);

  static constexpr uint32_t OffsetOfEntry(uint32_t i) {
    // Used in CodeAssembler::LookupExternalReference.
    return i * kEntrySize;
  }

  static void InitializeOncePerIsolateGroup(
      MemorySpan<Address> shared_external_references);
  static const char* NameOfIsolateIndependentAddress(
      Address address, MemorySpan<Address> shared_external_references);

  const char* NameFromOffset(uint32_t offset) {
    DCHECK_EQ(offset % kEntrySize, 0);
    DCHECK_LT(offset, kSizeInBytes);
    int index = offset / kEntrySize;
    return name(index);
  }

  ExternalReferenceTable() = default;
  ExternalReferenceTable(const ExternalReferenceTable&) = delete;
  ExternalReferenceTable& operator=(const ExternalReferenceTable&) = delete;

  void InitIsolateIndependent(
      MemorySpan<Address> shared_external_references);  // Step 1.

  void Init(Isolate* isolate);    // Step 2.

 private:
  static void AddIsolateIndependent(
      Address address, int* index,
      MemorySpan<Address> shared_external_references);

  static void AddIsolateIndependentReferences(
      int* index, MemorySpan<Address> shared_external_references);
  static void AddBuiltins(int* index,
                          MemorySpan<Address> shared_external_references);
  static void AddRuntimeFunctions(
      int* index, MemorySpan<Address> shared_external_references);
  static void AddAccessors(int* index,
                           MemorySpan<Address> shared_external_references);

  void Add(Address address, int* index);

  void CopyIsolateIndependentReferences(
      int* index, MemorySpan<Address> shared_external_references);
  void AddIsolateDependentReferences(Isolate* isolate, int* index);
  void AddIsolateAddresses(Isolate* isolate, int* index);
  void AddStubCache(Isolate* isolate, int* index);

  Address GetStatsCounterAddress(StatsCounter* counter);
  void AddNativeCodeStatsCounters(Isolate* isolate, int* index);

  static_assert(sizeof(Address) == kEntrySize);
#ifdef DEBUG
  Address ref_addr_[kSize] = {kNullAddress};
#else
  Address ref_addr_[kSize];
#endif  // DEBUG
  static const char* const ref_name_[kSize];

  enum InitializationState : uint32_t {
    kUninitialized,
    kInitializedIsolateIndependent,
    kInitialized,
  };
  InitializationState is_initialized_ = kUninitialized;

  // Redirect disabled stats counters to this field. This is done to make sure
  // we can have a snapshot that includes native counters even when the embedder
  // isn't collecting them.
  // This field is uint32_t since the MacroAssembler and CodeStubAssembler
  // accesses this field as a uint32_t.
  uint32_t dummy_stats_counter_ = 0;
};

static_assert(ExternalReferenceTable::kSizeInBytes ==
              sizeof(ExternalReferenceTable));

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_EXTERNAL_REFERENCE_TABLE_H_

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_CODE_SERIALIZER_H_
#define V8_SNAPSHOT_CODE_SERIALIZER_H_

#include "src/base/macros.h"
#include "src/codegen/script-details.h"
#include "src/snapshot/serializer.h"
#include "src/snapshot/snapshot-data.h"

namespace v8 {
namespace internal {

class PersistentHandles;
class BackgroundMergeTask;

class V8_EXPORT_PRIVATE AlignedCachedData {
 public:
  AlignedCachedData(const uint8_t* data, int length);
  ~AlignedCachedData() {
    if (owns_data_) DeleteArray(data_);
  }
  AlignedCachedData(const AlignedCachedData&) = delete;
  AlignedCachedData& operator=(const AlignedCachedData&) = delete;

  const uint8_t* data() const { return data_; }
  int length() const { return length_; }
  bool rejected() const { return rejected_; }

  void Reject() { rejected_ = true; }

  bool HasDataOwnership() const { return owns_data_; }

  void AcquireDataOwnership() {
    DCHECK(!owns_data_);
    owns_data_ = true;
  }

  void ReleaseDataOwnership() {
    DCHECK(owns_data_);
    owns_data_ = false;
  }

 private:
  bool owns_data_ : 1;
  bool rejected_ : 1;
  const uint8_t* data_;
  int length_;
};

typedef v8::ScriptCompiler::CachedData::CompatibilityCheckResult
    SerializedCodeSanityCheckResult;

// If this fails, update the static_assert AND the code_cache_reject_reason
// histogram definition.
static_assert(static_cast<int>(SerializedCodeSanityCheckResult::kLast) == 9);

class CodeSerializer : public Serializer {
 public:
  struct OffThreadDeserializeData {
   public:
    bool HasResult() const { return !maybe_result.is_null(); }
    Handle<Script> GetOnlyScript(LocalHeap* heap);

   private:
    friend class CodeSerializer;
    MaybeHandle<SharedFunctionInfo> maybe_result;
    std::vector<Handle<Script>> scripts;
    std::unique_ptr<PersistentHandles> persistent_handles;
    SerializedCodeSanityCheckResult sanity_check_result;
  };

  CodeSerializer(const CodeSerializer&) = delete;
  CodeSerializer& operator=(const CodeSerializer&) = delete;
  V8_EXPORT_PRIVATE static ScriptCompiler::CachedData* Serialize(
      Isolate* isolate, Handle<SharedFunctionInfo> info);

  AlignedCachedData* SerializeSharedFunctionInfo(
      Handle<SharedFunctionInfo> info);

  V8_WARN_UNUSED_RESULT static MaybeHandle<SharedFunctionInfo> Deserialize(
      Isolate* isolate, AlignedCachedData* cached_data, Handle<String> source,
      const ScriptDetails& script_details,
      MaybeHandle<Script> maybe_cached_script = {});

  V8_WARN_UNUSED_RESULT static OffThreadDeserializeData
  StartDeserializeOffThread(LocalIsolate* isolate,
                            AlignedCachedData* cached_data);

  V8_WARN_UNUSED_RESULT static MaybeHandle<SharedFunctionInfo>
  FinishOffThreadDeserialize(
      Isolate* isolate, OffThreadDeserializeData&& data,
      AlignedCachedData* cached_data, DirectHandle<String> source,
      const ScriptDetails& script_details,
      BackgroundMergeTask* background_merge_task = nullptr);

  uint32_t source_hash() const { return source_hash_; }

 protected:
  CodeSerializer(Isolate* isolate, uint32_t source_hash);
  ~CodeSerializer() override { OutputStatistics("CodeSerializer"); }

  void SerializeGeneric(Handle<HeapObject> heap_object, SlotType slot_type);

 private:
  void SerializeObjectImpl(Handle<HeapObject> o, SlotType slot_type) override;

  DISALLOW_GARBAGE_COLLECTION(no_gc_)
  uint32_t source_hash_;
};

// Wrapper around ScriptData to provide code-serializer-specific functionality.
class SerializedCodeData : public SerializedData {
 public:
  // The data header consists of uint32_t-sized entries:
  static const uint32_t kVersionHashOffset = kMagicNumberOffset + kUInt32Size;
  static const uint32_t kSourceHashOffset = kVersionHashOffset + kUInt32Size;
  static const uint32_t kFlagHashOffset = kSourceHashOffset + kUInt32Size;
  static const uint32_t kReadOnlySnapshotChecksumOffset =
      kFlagHashOffset + kUInt32Size;
  static const uint32_t kPayloadLengthOffset =
      kReadOnlySnapshotChecksumOffset + kUInt32Size;
  static const uint32_t kChecksumOffset = kPayloadLengthOffset + kUInt32Size;
  static const uint32_t kUnalignedHeaderSize = kChecksumOffset + kUInt32Size;
  static const uint32_t kHeaderSize = POINTER_SIZE_ALIGN(kUnalignedHeaderSize);

  // Used when consuming.
  static SerializedCodeData FromCachedData(
      Isolate* isolate, AlignedCachedData* cached_data,
      uint32_t expected_source_hash,
      SerializedCodeSanityCheckResult* rejection_result);
  // For cached data which is consumed before the source is available (e.g.
  // off-thread).
  static SerializedCodeData FromCachedDataWithoutSource(
      LocalIsolate* local_isolate, AlignedCachedData* cached_data,
      SerializedCodeSanityCheckResult* rejection_result);
  // For cached data which was previously already sanity checked by
  // FromCachedDataWithoutSource. The rejection result from that call should be
  // passed into this one.
  static SerializedCodeData FromPartiallySanityCheckedCachedData(
      AlignedCachedData* cached_data, uint32_t expected_source_hash,
      SerializedCodeSanityCheckResult* rejection_result);

  // Used when producing.
  SerializedCodeData(const std::vector<uint8_t>* payload,
                     const CodeSerializer* cs);

  // Return ScriptData object and relinquish ownership over it to the caller.
  AlignedCachedData* GetScriptData();

  base::Vector<const uint8_t> Payload() const;

  static uint32_t SourceHash(DirectHandle<String> source,
                             ScriptOriginOptions origin_options);

 private:
  explicit SerializedCodeData(AlignedCachedData* data);
  SerializedCodeData(const uint8_t* data, int size)
      : SerializedData(const_cast<uint8_t*>(data), size) {}

  base::Vector<const uint8_t> ChecksummedContent() const {
    return base::Vector<const uint8_t>(data_ + kHeaderSize,
                                       size_ - kHeaderSize);
  }

  SerializedCodeSanityCheckResult SanityCheck(
      uint32_t expected_ro_snapshot_checksum,
      uint32_t expected_source_hash) const;
  SerializedCodeSanityCheckResult SanityCheckJustSource(
      uint32_t expected_source_hash) const;
  SerializedCodeSanityCheckResult SanityCheckWithoutSource(
      uint32_t expected_ro_snapshot_checksum) const;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_CODE_SERIALIZER_H_
